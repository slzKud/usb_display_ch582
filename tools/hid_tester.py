#!/usr/bin/env python3
"""CH582 USB HID 测试工具"""

import struct
import threading
import tkinter as tk
from tkinter import ttk, scrolledtext, filedialog
import hid

# SPI Flash 芯片 ID 查找表
# 固件使用 JEDEC ID (0x9F) 命令，返回 3 字节: [manufacturer_id, memory_type, capacity_id]
# capacity_id 编码: 0x14=8Mbit, 0x15=16Mbit, 0x16=32Mbit, 0x17=64Mbit, 0x18=128Mbit, 0x19=256Mbit
W25Q_CHIP_TABLE = {
    0xEF: {  # Winbond
        0x16: ("W25Q32",   4 * 1024 * 1024),      # 4MB
        0x17: ("W25Q64",   8 * 1024 * 1024),      # 8MB
        0x18: ("W25Q128", 16 * 1024 * 1024),      # 16MB
    },
}

# DataFlash 大小
DATAFLASH_SIZE = 0x8000  # 32KB

# ─── 协议常量 ───
MAGIC = [0x44, 0x47]  # 'D', 'G'
CMD_MCU_VER = 0x00
CMD_PORT_INFO = 0x01
CMD_MCU_OPT = 0x02
CMD_SEND_DATA = 0x03
CMD_RECV_DATA = 0x04

MCU_OPT_GPIO_LEVEL = 0x01
MCU_OPT_GPIO_DIR = 0x02
MCU_OPT_SPI = 0x03
MCU_OPT_DATAFLASH = 0x04
MCU_OPT_VARIANT = 0x05
MCU_OPT_FATFS = 0x06

# FATFS actions
FATFS_CHECK_FORMATTED = 0x01
FATFS_SCAN_FILES = 0x02
FATFS_GET_FILE_INFO = 0x03
FATFS_READ_FILE = 0x04
FATFS_CREATE_FILE = 0x05
FATFS_WRITE_FILE = 0x06
FATFS_GET_FS_INFO = 0x07
FATFS_FORMAT = 0x08

# HID包64字节, 写入命令头: MAGIC(2)+CMD(1)+LEN(1)+ACTION(1)+FILE_INDEX(1)+OFFSET(4)+DATA_LEN(1)+CHK(1) = 12
# 可用数据空间: 64 - 12 = 52, 但LEN字段限制payload最大63, payload=ACTION+FILE_INDEX+OFFSET+DATA_LEN+DATA=8+DATA
# 所以DATA最大 = 64 - 2(MAGIC) - 1(CMD) - 1(LEN) - 8(头部) - 1(CHK) = 51
FATFS_WRITE_CHUNK = 51
FATFS_READ_CHUNK = 52

# SPI actions
SPI_GET_ID = 0x00
SPI_READ = 0x01
SPI_WRITE = 0x02
SPI_ERASE_CHIP = 0x03
SPI_ERASE_BLOCK = 0x04
SPI_GET_STATUS = 0x05

# DataFlash actions
DF_READ = 0x01
DF_WRITE = 0x02
DF_ERASE_ALL = 0x03

# GPIO
GPIO_OUTPUT = 0x00
GPIO_INPUT = 0x01
GPIO_LOW = 0x00
GPIO_HIGH = 0x01
GPIO_WRITE = 0x01
GPIO_READ = 0x00

# Variant types
VAR_INT = 0
VAR_FLOAT = 1
VAR_BOOL = 2
VAR_STR = 3
VAR_TYPE_NAMES = ["INT", "FLOAT", "BOOL", "STR"]

# Status names
STATUS_NAMES = {
    0x00: "SUCCESS",
    0x01: "FAILED",
    0x02: "CHECKSUM_ERROR",
    0x03: "PACKET_FORMAT_ERROR",
    0x04: "INVALID_COMMAND",
    0x05: "DATA_LEN_INVALID",
    0xFF: "ERROR",
}


def make_checksum(data):
    return sum(data) & 0xFF


def build_packet(cmd, payload):
    length = len(payload)
    pkt = [0x44, 0x47, cmd, length] + list(payload)
    pkt.append(make_checksum(pkt))
    return bytes(pkt) + b'\x00' * (64 - len(pkt))


def parse_response(raw):
    if len(raw) < 5:
        return None, None, "响应太短"
    d = list(raw[:5 + raw[3]])
    if d[0] != 0x44 or d[1] != 0x47:
        return None, None, "Magic 不匹配"
    pkt_data = d[:-1]
    if d[-1] != make_checksum(pkt_data):
        return None, None, "校验和错误"
    return d[2], d[4:4 + d[3]], None  # cmd, payload, error


def pack_variant(var_type, value):
    if var_type == VAR_INT:
        return struct.pack('<BHi', var_type, 4, int(value))
    elif var_type == VAR_FLOAT:
        return struct.pack('<BHf', var_type, 4, float(value))
    elif var_type == VAR_BOOL:
        v = 1 if str(value).lower() in ('1', 'true', 'yes') else 0
        return struct.pack('<BHB', var_type, 1, v)
    elif var_type == VAR_STR:
        b = str(value).encode('ascii')[:15]
        return struct.pack('<BH', var_type, len(b)) + b
    return b''


def parse_variant(data):
    if len(data) < 3:
        return None, None
    vtype = data[0]
    vlen = struct.unpack_from('<H', data, 1)[0]
    vdata = data[3:3 + vlen]
    if vtype == VAR_INT:
        return vtype, struct.unpack('<i', vdata)[0]
    elif vtype == VAR_FLOAT:
        return vtype, struct.unpack('<f', vdata)[0]
    elif vtype == VAR_BOOL:
        return vtype, bool(vdata[0])
    elif vtype == VAR_STR:
        return vtype, vdata.decode('ascii', errors='replace')
    return vtype, vdata.hex()


def hexdump(data, prefix=""):
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        hex_str = ' '.join(f'{b:02X}' for b in chunk)
        ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
        lines.append(f"{prefix}{i:04X}: {hex_str:<48s}  {ascii_str}")
    return '\n'.join(lines)


def lookup_spi_chip(id_bytes):
    """从 JEDEC ID 返回的字节查找芯片型号和大小。
    id_bytes 格式: [manufacturer_id, memory_type, capacity_id]
    返回 (chip_name, size_bytes) 或 (None, None)。"""
    if len(id_bytes) < 3:
        return None, None
    mfr, cap_id = id_bytes[0], id_bytes[2]
    if mfr in W25Q_CHIP_TABLE:
        chips = W25Q_CHIP_TABLE[mfr]
        if cap_id in chips:
            return chips[cap_id]
    hex_str = ' '.join(f'0x{b:02X}' for b in id_bytes)
    return f"未知({hex_str})", None


class HIDProtocol:
    def __init__(self, vid=0x2107, pid=0x413D):
        self.dev = hid.device()
        self.dev.open(vid, pid)
        self.dev.set_nonblocking(0)

    def close(self):
        self.dev.close()

    def send_recv(self, cmd, payload=b''):
        pkt = build_packet(cmd, list(payload) if isinstance(payload, (bytes, bytearray)) else payload)
        print("发送数据包：%s"%(pkt))
        self.dev.write([0]+list(pkt))
        raw = self.dev.read(64, timeout_ms=1000)
        print("收到数据包：%s"%(raw))
        if not raw:
            return None, None, "超时，无响应"
        return parse_response(raw)

    # ─── 基础命令 ───
    def get_mcu_version(self):
        resp_cmd, payload, err = self.send_recv(CMD_MCU_VER)
        if err:
            return None, None, err
        if len(payload) < 2:
            return None, None, "payload 长度不足"
        return payload[0], payload[1], None

    def get_port_info(self, port):
        resp_cmd, payload, err = self.send_recv(CMD_PORT_INFO, [port])
        if err:
            return None, err
        if len(payload) < 4:
            return None, "payload 长度不足"
        return {"port": payload[0], "connection": payload[1]}, None

    # ─── GPIO ───
    def gpio_set_direction(self, gpio, direction):
        resp_cmd, payload, err = self.send_recv(CMD_MCU_OPT, [MCU_OPT_GPIO_DIR, gpio, direction])
        if err:
            return None, err
        return {"status": payload[0], "gpio": payload[1], "direction": payload[2]}, None

    def gpio_write(self, gpio, level):
        resp_cmd, payload, err = self.send_recv(CMD_MCU_OPT, [MCU_OPT_GPIO_LEVEL, gpio, GPIO_WRITE, level])
        if err:
            return None, err
        return {"status": payload[0], "gpio": payload[1], "level": payload[2]}, None

    def gpio_read(self, gpio):
        resp_cmd, payload, err = self.send_recv(CMD_MCU_OPT, [MCU_OPT_GPIO_LEVEL, gpio, GPIO_READ])
        if err:
            return None, err
        return {"status": payload[0], "gpio": payload[1], "level": payload[2]}, None

    # ─── SPI Flash ───
    def spi_get_id(self):
        resp_cmd, payload, err = self.send_recv(CMD_MCU_OPT, [MCU_OPT_SPI, SPI_GET_ID])
        if err:
            return None, err
        return payload[2:5], None  # skip status + action, 3 bytes JEDEC ID

    def spi_read(self, offset, length):
        data = [MCU_OPT_SPI, SPI_READ] + list(struct.pack('<I', offset)) + list(struct.pack('<H', length))
        resp_cmd, payload, err = self.send_recv(CMD_MCU_OPT, data)
        if err:
            return None, err
        return bytes(payload[2:]), None  # skip status + action

    def spi_write(self, offset, data):
        payload = [MCU_OPT_SPI, SPI_WRITE] + list(struct.pack('<I', offset)) + list(struct.pack('<H', len(data))) + list(data)
        resp_cmd, resp_payload, err = self.send_recv(CMD_MCU_OPT, payload)
        if err:
            return None, err
        return resp_payload[0], None  # status

    def spi_erase_chip(self):
        resp_cmd, payload, err = self.send_recv(CMD_MCU_OPT, [MCU_OPT_SPI, SPI_ERASE_CHIP])
        if err:
            return None, err
        return payload[0], None

    def spi_erase_block(self, offset):
        data = [MCU_OPT_SPI, SPI_ERASE_BLOCK] + list(struct.pack('<I', offset))
        resp_cmd, payload, err = self.send_recv(CMD_MCU_OPT, data)
        if err:
            return None, err
        return payload[0], None

    def spi_get_status(self):
        resp_cmd, payload, err = self.send_recv(CMD_MCU_OPT, [MCU_OPT_SPI, SPI_GET_STATUS])
        if err:
            return None, err
        return payload[2], None  # spi_status: 0x00=OK, 0x02=BUSY

    def spi_wait_ready(self, timeout_ms=30000, poll_interval_ms=100):
        """轮询 SPI flash 状态直到就绪或超时。返回 (ready, err)。"""
        import time
        deadline = time.time() + timeout_ms / 1000.0
        while time.time() < deadline:
            status, err = self.spi_get_status()
            if err:
                return False, err
            if status == 0x00:  # W25Qx_OK
                return True, None
            time.sleep(poll_interval_ms / 1000.0)
        return False, "轮询超时"

    # ─── DataFlash ───
    def dataflash_read(self, offset, length):
        data = [MCU_OPT_DATAFLASH, DF_READ] + list(struct.pack('<H', offset)) + list(struct.pack('<H', length))
        resp_cmd, payload, err = self.send_recv(CMD_MCU_OPT, data)
        if err:
            return None, err
        return bytes(payload[2:]), None

    def dataflash_write(self, offset, data):
        payload = [MCU_OPT_DATAFLASH, DF_WRITE] + list(struct.pack('<H', offset)) + list(struct.pack('<H', len(data))) + list(data)
        resp_cmd, resp_payload, err = self.send_recv(CMD_MCU_OPT, payload)
        if err:
            return None, err
        return resp_payload[0], None

    def dataflash_erase_all(self):
        resp_cmd, payload, err = self.send_recv(CMD_MCU_OPT, [MCU_OPT_DATAFLASH, DF_ERASE_ALL])
        if err:
            return None, err
        return payload[0], None

    # ─── Variant ───
    def variant_write(self, slot_id, var_type, value):
        packed = pack_variant(var_type, value)
        payload = [MCU_OPT_VARIANT, slot_id] + list(packed)
        resp_cmd, resp_payload, err = self.send_recv(CMD_MCU_OPT, payload)
        if err:
            return None, err
        return resp_payload[0], None  # status

    def variant_read(self, slot_id):
        resp_cmd, payload, err = self.send_recv(CMD_MCU_OPT, [MCU_OPT_VARIANT, slot_id])
        if err:
            return None, None, err
        if payload[0] != 0:
            return None, None, f"状态: {STATUS_NAMES.get(payload[0], 'UNKNOWN')}"
        return parse_variant(payload[2:])  # skip status + var_id

    # ─── FATFS ───
    def fatfs_check_formatted(self):
        resp_cmd, payload, err = self.send_recv(CMD_MCU_OPT, [MCU_OPT_FATFS, FATFS_CHECK_FORMATTED])
        if err:
            return None, err
        return payload[0], None  # 0x00=已格式化, 0x01=未格式化

    def fatfs_format(self):
        resp_cmd, payload, err = self.send_recv(CMD_MCU_OPT, [MCU_OPT_FATFS, FATFS_FORMAT])
        if err:
            return None, err
        return payload[0], None

    def fatfs_scan_files(self):
        resp_cmd, payload, err = self.send_recv(CMD_MCU_OPT, [MCU_OPT_FATFS, FATFS_SCAN_FILES])
        if err:
            return None, err
        return payload[2], None  # file_count: payload=[STATUS, ACTION, COUNT]

    def fatfs_get_file_info(self, file_index):
        resp_cmd, payload, err = self.send_recv(CMD_MCU_OPT, [MCU_OPT_FATFS, FATFS_GET_FILE_INFO, file_index])
        if err:
            return None, err
        if payload[0] != 0:
            return None, f"状态: {STATUS_NAMES.get(payload[0], 'UNKNOWN')}"
        # payload: [STATUS][ACTION][FILESIZE(4B)][TOTAL_CHUNKS(2B)][FILENAME\0]
        p = bytes(payload)
        fsize = struct.unpack_from('<I', p, 2)[0]
        total_chunks = struct.unpack_from('<H', p, 6)[0]
        fname = p[8:].split(b'\x00')[0].decode('ascii', errors='replace')
        return {"size": fsize, "chunks": total_chunks, "name": fname}, None

    def fatfs_read_file(self, file_index, chunk_index):
        data = [MCU_OPT_FATFS, FATFS_READ_FILE, file_index] + list(struct.pack('<H', chunk_index))
        resp_cmd, payload, err = self.send_recv(CMD_MCU_OPT, data)
        if err:
            return None, err
        if payload[0] != 0:
            return None, f"状态: {STATUS_NAMES.get(payload[0], 'UNKNOWN')}"
        # payload: [STATUS][ACTION][CHUNK_INDEX(2B)][BYTES_READ(1B)][DATA...]
        bytes_read = payload[4]
        return bytes(payload[5:5 + bytes_read]), None

    def fatfs_create_file(self, filename, file_size):
        fname_bytes = filename.encode('ascii')
        data = [MCU_OPT_FATFS, FATFS_CREATE_FILE, len(fname_bytes)] + list(fname_bytes) + list(struct.pack('<I', file_size))
        resp_cmd, payload, err = self.send_recv(CMD_MCU_OPT, data)
        if err:
            return None, err
        return payload[0], None

    def fatfs_write_file(self, file_index, offset, data):
        payload = [MCU_OPT_FATFS, FATFS_WRITE_FILE, file_index] + list(struct.pack('<I', offset))
        payload.append(len(data))
        payload.extend(data)
        resp_cmd, resp_payload, err = self.send_recv(CMD_MCU_OPT, payload)
        if err:
            return None, err
        return resp_payload[0], None

    def fatfs_get_fs_info(self):
        resp_cmd, payload, err = self.send_recv(CMD_MCU_OPT, [MCU_OPT_FATFS, FATFS_GET_FS_INFO])
        if err:
            return None, err
        if payload[0] != 0:
            return None, f"状态: {STATUS_NAMES.get(payload[0], 'UNKNOWN')}"
        p = bytes(payload)
        total = struct.unpack_from('<I', p, 2)[0]
        free = struct.unpack_from('<I', p, 6)[0]
        return {"total": total, "free": free}, None


class HIDTesterGUI:
    def __init__(self):
        self.proto = None
        self.root = tk.Tk()
        self.root.title("CH582 HID 测试工具")
        self.root.geometry("750x680")
        self.root.resizable(True, True)
        self._build_ui()

    def log(self, msg):
        print(msg);
        self.log_text.configure(state='normal')
        self.log_text.insert('end', msg + '\n')
        self.log_text.see('end')
        self.log_text.configure(state='disabled')

    def log_packet(self, direction, data):
        arrow = ">>>" if direction == "TX" else "<<<"
        self.log(f"{arrow} {data.hex(' ')}")

    def _build_ui(self):
        # 顶部连接栏
        top = ttk.Frame(self.root, padding=5)
        top.pack(fill='x')
        self.conn_status = tk.StringVar(value="未连接")
        ttk.Label(top, textvariable=self.conn_status).pack(side='left')
        ttk.Button(top, text="连接", command=self.connect).pack(side='left', padx=5)
        ttk.Button(top, text="断开", command=self.disconnect).pack(side='left')

        # Notebook
        self.notebook = ttk.Notebook(self.root)
        self.notebook.pack(fill='both', expand=True, padx=5, pady=5)

        self._build_tab_device()
        self._build_tab_variant()
        self._build_tab_gpio()
        self._build_tab_dataflash()
        self._build_tab_spi()
        self._build_tab_fatfs()

        # 底部日志
        log_frame = ttk.LabelFrame(self.root, text="通信日志", padding=5)
        log_frame.pack(fill='both', expand=False, padx=5, pady=(0, 5))
        self.log_text = scrolledtext.ScrolledText(log_frame, height=8, state='disabled', font=('Consolas', 9))
        self.log_text.pack(fill='both', expand=True)
        ttk.Button(log_frame, text="清空日志", command=self.clear_log).pack(anchor='e', pady=(3, 0))

    # ─── 连接 ───
    def connect(self):
        try:
            self.proto = HIDProtocol(0x413d, 0x2107)
            self.conn_status.set("已连接 (VID:0x413d PID:0x2107)")
            self.log("设备已连接")
        except Exception as e:
            self.conn_status.set("连接失败")
            self.log(f"连接失败: {e}")

    def disconnect(self):
        if self.proto:
            self.proto.close()
            self.proto = None
        self.conn_status.set("未连接")
        self.log("设备已断开")

    def clear_log(self):
        self.log_text.configure(state='normal')
        self.log_text.delete('1.0', 'end')
        self.log_text.configure(state='disabled')

    def _check_conn(self):
        if not self.proto:
            self.log("请先连接设备")
            return False
        return True

    # ─── Tab: 设备信息 ───
    def _build_tab_device(self):
        tab = ttk.Frame(self.notebook, padding=10)
        self.notebook.add(tab, text="设备信息")

        row = ttk.Frame(tab)
        row.pack(fill='x', pady=5)
        ttk.Button(row, text="获取 MCU 版本", command=self.on_get_version).pack(side='left')
        self.ver_result = tk.StringVar()
        ttk.Label(row, textvariable=self.ver_result).pack(side='left', padx=10)

        row2 = ttk.Frame(tab)
        row2.pack(fill='x', pady=5)
        ttk.Label(row2, text="Port:").pack(side='left')
        self.port_var = tk.IntVar(value=0)
        ttk.Spinbox(row2, from_=0, to=1, textvariable=self.port_var, width=3).pack(side='left', padx=5)
        ttk.Button(row2, text="查询 Port Info", command=self.on_get_port_info).pack(side='left')
        self.port_result = tk.StringVar()
        ttk.Label(row2, textvariable=self.port_result).pack(side='left', padx=10)

    def on_get_version(self):
        if not self._check_conn():
            return
        major, minor, err = self.proto.get_mcu_version()
        if err:
            self.log(f"获取版本失败: {err}")
            self.ver_result.set(f"失败: {err}")
        else:
            self.log(f"MCU 版本: {major}.{minor}")
            self.ver_result.set(f"v{major}.{minor}")

    def on_get_port_info(self):
        if not self._check_conn():
            return
        result, err = self.proto.get_port_info(self.port_var.get())
        if err:
            self.log(f"获取 Port Info 失败: {err}")
            self.port_result.set(f"失败: {err}")
        else:
            conn_str = "已连接" if result["connection"] else "未连接"
            self.log(f"Port {result['port']}: {conn_str}")
            self.port_result.set(f"Port {result['port']}: {conn_str}")

    # ─── Tab: Variant ───
    def _build_tab_variant(self):
        tab = ttk.Frame(self.notebook, padding=10)
        self.notebook.add(tab, text="Variant 控制")

        # Slot
        row1 = ttk.Frame(tab)
        row1.pack(fill='x', pady=5)
        ttk.Label(row1, text="Slot:").pack(side='left')
        self.var_slot = tk.IntVar(value=0)
        slot_combo = ttk.Combobox(row1, textvariable=self.var_slot, values=list(range(16)), width=4, state='readonly')
        slot_combo.pack(side='left', padx=5)
        ttk.Label(row1, text="(映射 0xF0~0xFF)").pack(side='left')

        # Type
        row2 = ttk.Frame(tab)
        row2.pack(fill='x', pady=5)
        ttk.Label(row2, text="类型:").pack(side='left')
        self.var_type = tk.StringVar(value="INT")
        type_combo = ttk.Combobox(row2, textvariable=self.var_type, values=VAR_TYPE_NAMES, width=8, state='readonly')
        type_combo.pack(side='left', padx=5)
        type_combo.bind('<<ComboboxSelected>>', self._on_var_type_changed)

        # Value
        row3 = ttk.Frame(tab)
        row3.pack(fill='x', pady=5)
        ttk.Label(row3, text="值:").pack(side='left')
        self.var_value = tk.StringVar(value="0")
        self.var_entry = ttk.Entry(row3, textvariable=self.var_value, width=30)
        self.var_entry.pack(side='left', padx=5)

        # Bool 特殊控件
        self.var_bool = tk.BooleanVar(value=False)
        self.bool_combo = ttk.Combobox(row3, textvariable=self.var_bool,
                                       values=["False", "True"], width=8, state='readonly')

        # Buttons
        row4 = ttk.Frame(tab)
        row4.pack(fill='x', pady=5)
        ttk.Button(row4, text="写入 Variant", command=self.on_var_write).pack(side='left')
        ttk.Button(row4, text="读取 Variant", command=self.on_var_read).pack(side='left', padx=10)
        self.var_read_result = tk.StringVar()
        ttk.Label(row4, textvariable=self.var_read_result).pack(side='left', padx=10)

    def _on_var_type_changed(self, event=None):
        t = self.var_type.get()
        if t == "BOOL":
            self.var_entry.pack_forget()
            self.bool_combo.pack(side='left', padx=5)
        else:
            self.bool_combo.pack_forget()
            self.var_entry.pack(side='left', padx=5)
            if t == "INT":
                self.var_value.set("0")
            elif t == "FLOAT":
                self.var_value.set("0.0")
            elif t == "STR":
                self.var_value.set("hello")

    def _get_var_type_enum(self):
        return VAR_TYPE_NAMES.index(self.var_type.get())

    def _get_var_value(self):
        t = self._get_var_type_enum()
        if t == VAR_BOOL:
            return self.var_bool.get()
        return self.var_value.get()

    def on_var_write(self):
        if not self._check_conn():
            return
        slot = 0xF0 + self.var_slot.get()
        vtype = self._get_var_type_enum()
        value = self._get_var_value()
        status, err = self.proto.variant_write(slot, vtype, value)
        if err:
            self.log(f"Variant 写入失败: {err}")
        else:
            if(status>1):
                self.log(f"Variant 写入 slot=0x{slot:02X} type={VAR_TYPE_NAMES[vtype]} value={value} → 错误状态码:{status-1}")
            else:
                self.log(f"Variant 写入 slot=0x{slot:02X} type={VAR_TYPE_NAMES[vtype]} value={value} → {STATUS_NAMES.get(status, '?')}")

    def on_var_read(self):
        if not self._check_conn():
            return
        slot = 0xF0 + self.var_slot.get()
        vtype, value, err = self.proto.variant_read(slot)
        if err:
            self.log(f"Variant 读取失败: {err}")
            self.var_read_result.set(f"失败: {err}")
        else:
            self.log(f"Variant 读取 slot=0x{slot:02X}: type={VAR_TYPE_NAMES[vtype]} value={value}")
            self.var_read_result.set(f"{VAR_TYPE_NAMES[vtype]}: {value}")

    # ─── Tab: GPIO ───
    def _build_tab_gpio(self):
        tab = ttk.Frame(self.notebook, padding=10)
        self.notebook.add(tab, text="GPIO 控制")

        row1 = ttk.Frame(tab)
        row1.pack(fill='x', pady=5)
        ttk.Label(row1, text="GPIO 编号:").pack(side='left')
        self.gpio_num = tk.IntVar(value=0)
        ttk.Spinbox(row1, from_=0, to=1, textvariable=self.gpio_num, width=3).pack(side='left', padx=5)

        row2 = ttk.Frame(tab)
        row2.pack(fill='x', pady=5)
        ttk.Label(row2, text="方向:").pack(side='left')
        self.gpio_dir = tk.StringVar(value="OUTPUT")
        ttk.Combobox(row2, textvariable=self.gpio_dir, values=["OUTPUT", "INPUT"], width=8, state='readonly').pack(side='left', padx=5)
        ttk.Button(row2, text="设置方向", command=self.on_gpio_set_dir).pack(side='left', padx=5)

        row3 = ttk.Frame(tab)
        row3.pack(fill='x', pady=5)
        ttk.Label(row3, text="电平:").pack(side='left')
        self.gpio_level = tk.StringVar(value="HIGH")
        ttk.Combobox(row3, textvariable=self.gpio_level, values=["HIGH", "LOW"], width=8, state='readonly').pack(side='left', padx=5)
        ttk.Button(row3, text="写入电平", command=self.on_gpio_write).pack(side='left', padx=5)
        ttk.Button(row3, text="读取电平", command=self.on_gpio_read).pack(side='left', padx=5)
        self.gpio_read_result = tk.StringVar()
        ttk.Label(row3, textvariable=self.gpio_read_result).pack(side='left', padx=10)

    def on_gpio_set_dir(self):
        if not self._check_conn():
            return
        gpio = self.gpio_num.get()
        direction = GPIO_OUTPUT if self.gpio_dir.get() == "OUTPUT" else GPIO_INPUT
        result, err = self.proto.gpio_set_direction(gpio, direction)
        if err:
            self.log(f"GPIO 方向设置失败: {err}")
        else:
            self.log(f"GPIO {result['gpio']} 方向={self.gpio_dir.get()} → {STATUS_NAMES.get(result['status'], '?')}")

    def on_gpio_write(self):
        if not self._check_conn():
            return
        gpio = self.gpio_num.get()
        level = GPIO_HIGH if self.gpio_level.get() == "HIGH" else GPIO_LOW
        result, err = self.proto.gpio_write(gpio, level)
        if err:
            self.log(f"GPIO 写入失败: {err}")
        else:
            self.log(f"GPIO {result['gpio']} → {self.gpio_level.get()} ({STATUS_NAMES.get(result['status'], '?')})")

    def on_gpio_read(self):
        if not self._check_conn():
            return
        gpio = self.gpio_num.get()
        result, err = self.proto.gpio_read(gpio)
        if err:
            self.log(f"GPIO 读取失败: {err}")
            self.gpio_read_result.set(f"失败: {err}")
        else:
            level_str = "HIGH" if result['level'] else "LOW"
            self.log(f"GPIO {result['gpio']} = {level_str}")
            self.gpio_read_result.set(level_str)

    # ─── Tab: DataFlash ───
    def _build_tab_dataflash(self):
        tab = ttk.Frame(self.notebook, padding=10)
        self.notebook.add(tab, text="DataFlash")

        row1 = ttk.Frame(tab)
        row1.pack(fill='x', pady=5)
        ttk.Label(row1, text="偏移(hex):").pack(side='left')
        self.df_offset = tk.StringVar(value="0x0000")
        ttk.Entry(row1, textvariable=self.df_offset, width=10).pack(side='left', padx=5)
        ttk.Label(row1, text="长度:").pack(side='left')
        self.df_length = tk.IntVar(value=32)
        ttk.Spinbox(row1, from_=1, to=52, textvariable=self.df_length, width=5).pack(side='left', padx=5)
        ttk.Button(row1, text="读取", command=self.on_df_read).pack(side='left', padx=5)

        row2 = ttk.Frame(tab)
        row2.pack(fill='x', pady=5)
        ttk.Label(row2, text="写入数据(hex, 如 48656C6C6F):").pack(side='left')
        self.df_write_data = tk.StringVar()
        ttk.Entry(row2, textvariable=self.df_write_data, width=40).pack(side='left', padx=5)
        ttk.Button(row2, text="写入", command=self.on_df_write).pack(side='left', padx=5)

        row3 = ttk.Frame(tab)
        row3.pack(fill='x', pady=5)
        ttk.Button(row3, text="全部擦除 (0x0000~0x8000)", command=self.on_df_erase).pack(side='left')

        row4 = ttk.Frame(tab)
        row4.pack(fill='x', pady=5)
        ttk.Button(row4, text="完整读取 (32KB)", command=self.on_df_read_all).pack(side='left')
        ttk.Button(row4, text="保存到文件", command=self.on_df_save_to_file).pack(side='left', padx=5)
        self.df_progress = ttk.Progressbar(row4, length=200, mode='determinate')
        self.df_progress.pack(side='left', padx=5)
        self.df_progress_label = tk.StringVar(value="")
        ttk.Label(row4, textvariable=self.df_progress_label).pack(side='left')

        # 完整写入区域
        sep = ttk.Separator(tab, orient='horizontal')
        sep.pack(fill='x', pady=8)

        row5 = ttk.Frame(tab)
        row5.pack(fill='x', pady=5)
        ttk.Label(row5, text="从文件完整写入:", font=('', 10, 'bold')).pack(side='left')

        row6 = ttk.Frame(tab)
        row6.pack(fill='x', pady=3)
        ttk.Label(row6, text="文件:").pack(side='left')
        self.df_write_file = tk.StringVar()
        ttk.Entry(row6, textvariable=self.df_write_file, width=40, state='readonly').pack(side='left', padx=5)
        ttk.Button(row6, text="选择文件...", command=self._on_df_choose_file).pack(side='left')

        row7 = ttk.Frame(tab)
        row7.pack(fill='x', pady=3)
        self.df_opt_backup = tk.BooleanVar(value=True)
        ttk.Checkbutton(row7, text="写入前备份旧数据", variable=self.df_opt_backup).pack(side='left')
        self.df_opt_erase = tk.BooleanVar(value=True)
        ttk.Checkbutton(row7, text="完整擦除 (否则只擦除写入区域)", variable=self.df_opt_erase).pack(side='left', padx=15)
        self.df_opt_verify = tk.BooleanVar(value=True)
        ttk.Checkbutton(row7, text="写入后校验", variable=self.df_opt_verify).pack(side='left', padx=15)

        row8 = ttk.Frame(tab)
        row8.pack(fill='x', pady=5)
        ttk.Button(row8, text="完整写入", command=self.on_df_write_all).pack(side='left')
        self.df_write_progress = ttk.Progressbar(row8, length=200, mode='determinate')
        self.df_write_progress.pack(side='left', padx=5)
        self.df_write_progress_label = tk.StringVar(value="")
        ttk.Label(row8, textvariable=self.df_write_progress_label).pack(side='left')

        # 读取结果显示
        self.df_result = scrolledtext.ScrolledText(tab, height=6, font=('Consolas', 9))
        self.df_result.pack(fill='both', expand=True, pady=5)

        # 完整读取的数据缓存
        self._df_full_data = None

    def on_df_read(self):
        if not self._check_conn():
            return
        offset = int(self.df_offset.get(), 16)
        length = self.df_length.get()
        data, err = self.proto.dataflash_read(offset, length)
        if err:
            self.log(f"DataFlash 读取失败: {err}")
        else:
            self.log(f"DataFlash 读取 0x{offset:04X} ({length} bytes)")
            self.df_result.delete('1.0', 'end')
            self.df_result.insert('1.0', hexdump(data))

    def on_df_write(self):
        if not self._check_conn():
            return
        offset = int(self.df_offset.get(), 16)
        hex_str = self.df_write_data.get().replace(' ', '').replace(',', '')
        data = bytes.fromhex(hex_str)
        status, err = self.proto.dataflash_write(offset, data)
        if err:
            self.log(f"DataFlash 写入失败: {err}")
        else:
            self.log(f"DataFlash 写入 0x{offset:04X} ({len(data)} bytes) → {STATUS_NAMES.get(status, '?')}")

    def on_df_erase(self):
        if not self._check_conn():
            return
        status, err = self.proto.dataflash_erase_all()
        if err:
            self.log(f"DataFlash 擦除失败: {err}")
        else:
            self.log(f"DataFlash 全部擦除 → {STATUS_NAMES.get(status, '?')}")

    def on_df_read_all(self):
        if not self._check_conn():
            return
        self.df_progress['value'] = 0
        self.df_progress['maximum'] = DATAFLASH_SIZE
        self.df_progress_label.set("0%")
        self.log(f"DataFlash 完整读取开始 (0x0000~0x{DATAFLASH_SIZE:04X}, {DATAFLASH_SIZE} bytes)")
        threading.Thread(target=self._df_read_all_thread, daemon=True).start()

    def _df_read_all_thread(self):
        total = DATAFLASH_SIZE
        chunk_size = 52
        buf = bytearray()
        for offset in range(0, total, chunk_size):
            n = min(chunk_size, total - offset)
            data, err = self.proto.dataflash_read(offset, n)
            if err:
                self.root.after(0, self._df_read_all_done, None, f"读取失败 @ 0x{offset:04X}: {err}")
                return
            buf.extend(data)
            pct = len(buf) * 100 // total
            self.root.after(0, self._df_read_all_progress, pct)
        self.root.after(0, self._df_read_all_done, bytes(buf), None)

    def _df_read_all_progress(self, pct):
        self.df_progress['value'] = DATAFLASH_SIZE * pct // 100
        self.df_progress_label.set(f"{pct}%")

    def _df_read_all_done(self, data, err):
        if err:
            self.log(f"DataFlash {err}")
            self.df_progress_label.set("失败")
            return
        self._df_full_data = data
        self.df_progress['value'] = DATAFLASH_SIZE
        self.df_progress_label.set("完成")
        self.log(f"DataFlash 完整读取完成 ({len(data)} bytes)")
        self.df_result.delete('1.0', 'end')
        self.df_result.insert('1.0', hexdump(data))

    def on_df_save_to_file(self):
        if self._df_full_data is None:
            self.log("请先执行完整读取")
            return
        path = filedialog.asksaveasfilename(
            defaultextension=".bin",
            filetypes=[("Binary", "*.bin"), ("All Files", "*.*")],
            title="保存 DataFlash 数据"
        )
        if path:
            with open(path, 'wb') as f:
                f.write(self._df_full_data)
            self.log(f"DataFlash 数据已保存到: {path} ({len(self._df_full_data)} bytes)")

    def _on_df_choose_file(self):
        path = filedialog.askopenfilename(
            filetypes=[("Binary", "*.bin"), ("All Files", "*.*")],
            title="选择 DataFlash 镜像文件"
        )
        if path:
            # 校验文件大小
            import os
            fsize = os.path.getsize(path)
            if fsize > DATAFLASH_SIZE:
                self.log(f"文件大小 ({fsize} bytes = 0x{fsize:04X}) 超过 DataFlash 容量 ({DATAFLASH_SIZE} bytes)，操作取消")
                tk.messagebox.showwarning("文件过大",
                    f"文件大小: {fsize} bytes (0x{fsize:04X})\n"
                    f"DataFlash 容量: {DATAFLASH_SIZE} bytes (0x{DATAFLASH_SIZE:04X})\n\n"
                    f"文件超出容量 {fsize - DATAFLASH_SIZE} bytes，无法写入。")
                return
            self.df_write_file.set(path)
            self.log(f"已选择文件: {path} ({fsize} bytes)")

    def on_df_write_all(self):
        if not self._check_conn():
            return
        path = self.df_write_file.get()
        if not path:
            self.log("请先选择文件")
            return
        with open(path, 'rb') as f:
            file_data = f.read()
        if len(file_data) > DATAFLASH_SIZE:
            self.log(f"文件过大 ({len(file_data)} > {DATAFLASH_SIZE})，取消写入")
            return
        if len(file_data) == 0:
            self.log("文件为空，取消写入")
            return
        # 对齐到 256 字节页
        padded = file_data + b'\xFF' * ((256 - len(file_data) % 256) % 256)
        do_backup = self.df_opt_backup.get()
        do_erase = self.df_opt_erase.get()
        do_verify = self.df_opt_verify.get()
        self.df_write_progress['value'] = 0
        self.df_write_progress['maximum'] = 100
        self.df_write_progress_label.set("0%")
        self.log(f"DataFlash 完整写入开始: {len(file_data)} bytes (填充至 {len(padded)} bytes)")
        threading.Thread(target=self._df_write_all_thread,
                         args=(padded, do_backup, do_erase, do_verify),
                         daemon=True).start()

    def _df_write_all_thread(self, data, do_backup, do_erase, do_verify):
        total = len(data)
        chunk_size = 52

        # Step 1: 备份
        if do_backup:
            self.root.after(0, self._df_write_progress_update, 0, "备份中...")
            backup = bytearray()
            for offset in range(0, total, chunk_size):
                n = min(chunk_size, total - offset)
                chunk, err = self.proto.dataflash_read(offset, n)
                if err:
                    self.root.after(0, self._df_write_all_done, False, f"备份读取失败 @ 0x{offset:04X}: {err}")
                    return
                backup.extend(chunk)
                pct = (offset + n) * 10 // total
                self.root.after(0, self._df_write_progress_update, pct, None)
            # 备份数据保存到本地
            self._df_save_backup(bytes(backup))

        # Step 2: 擦除
        if do_erase:
            self.root.after(0, self._df_write_progress_update, 0, "擦除中...")
            _, err = self.proto.dataflash_erase_all()
            if err:
                self.root.after(0, self._df_write_all_done, False, f"擦除失败: {err}")
                return
            self.root.after(0, self.log, "DataFlash 擦除完成")

        # Step 3: 写入
        self.root.after(0, self._df_write_progress_update, 0, "写入中...")
        for offset in range(0, total, chunk_size):
            n = min(chunk_size, total - offset)
            chunk = data[offset:offset + n]
            _, err = self.proto.dataflash_write(offset, chunk)
            if err:
                self.root.after(0, self._df_write_all_done, False, f"写入失败 @ 0x{offset:04X}: {err}")
                return
            pct = (offset + n) * 50 // total
            self.root.after(0, self._df_write_progress_update, pct, None)

        # Step 4: 校验
        if do_verify:
            self.root.after(0, self._df_write_progress_update, 50, "校验中...")
            for offset in range(0, total, chunk_size):
                n = min(chunk_size, total - offset)
                chunk, err = self.proto.dataflash_read(offset, n)
                if err:
                    self.root.after(0, self._df_write_all_done, False, f"校验读取失败 @ 0x{offset:04X}: {err}")
                    return
                if chunk != data[offset:offset + n]:
                    self.root.after(0, self._df_write_all_done, False,
                                    f"校验失败 @ 0x{offset:04X}: 数据不匹配")
                    return
                pct = 50 + (offset + n) * 50 // total
                self.root.after(0, self._df_write_progress_update, pct, None)

        self.root.after(0, self._df_write_all_done, True, None)

    def _df_write_progress_update(self, pct, msg):
        if pct is not None:
            self.df_write_progress['value'] = pct
        if msg:
            self.df_write_progress_label.set(msg)
        else:
            cur = self.df_write_progress_label.get()
            # 只更新百分比部分
            if '%' in cur and ' ' in cur:
                stage = cur.split(' ')[0]
                self.df_write_progress_label.set(f"{stage} {pct}%")
            else:
                self.df_write_progress_label.set(f"{pct}%")

    def _df_write_all_done(self, _success, err):
        if err:
            self.log(f"DataFlash 完整写入失败: {err}")
            self.df_write_progress_label.set("失败")
            return
        self.df_write_progress['value'] = 100
        self.df_write_progress_label.set("完成")
        self.log("DataFlash 完整写入成功")

    def _df_save_backup(self, backup_data):
        import os
        from datetime import datetime
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        save_dir = os.path.dirname(os.path.abspath(__file__))
        path = os.path.join(save_dir, f"dataflash_backup_{ts}.bin")
        with open(path, 'wb') as f:
            f.write(backup_data)
        self.log(f"DataFlash 备份已保存: {path} ({len(backup_data)} bytes)")

    # ─── Tab: SPI Flash ───
    def _build_tab_spi(self):
        tab = ttk.Frame(self.notebook, padding=10)
        self.notebook.add(tab, text="SPI Flash")

        row0 = ttk.Frame(tab)
        row0.pack(fill='x', pady=5)
        ttk.Button(row0, text="读取 Flash ID", command=self.on_spi_id).pack(side='left')
        self.spi_id_result = tk.StringVar()
        ttk.Label(row0, textvariable=self.spi_id_result).pack(side='left', padx=10)

        row1 = ttk.Frame(tab)
        row1.pack(fill='x', pady=5)
        ttk.Label(row1, text="偏移(hex):").pack(side='left')
        self.spi_offset = tk.StringVar(value="0x00000000")
        ttk.Entry(row1, textvariable=self.spi_offset, width=14).pack(side='left', padx=5)
        ttk.Label(row1, text="长度:").pack(side='left')
        self.spi_length = tk.IntVar(value=32)
        ttk.Spinbox(row1, from_=1, to=52, textvariable=self.spi_length, width=5).pack(side='left', padx=5)
        ttk.Button(row1, text="读取", command=self.on_spi_read).pack(side='left', padx=5)

        row2 = ttk.Frame(tab)
        row2.pack(fill='x', pady=5)
        ttk.Label(row2, text="写入数据(hex):").pack(side='left')
        self.spi_write_data = tk.StringVar()
        ttk.Entry(row2, textvariable=self.spi_write_data, width=40).pack(side='left', padx=5)
        ttk.Button(row2, text="写入", command=self.on_spi_write).pack(side='left', padx=5)

        row3 = ttk.Frame(tab)
        row3.pack(fill='x', pady=5)
        ttk.Button(row3, text="全片擦除", command=self.on_spi_erase_chip).pack(side='left')
        ttk.Label(row3, text="块偏移(hex):").pack(side='left', padx=(20, 0))
        self.spi_block_offset = tk.StringVar(value="0x00000000")
        ttk.Entry(row3, textvariable=self.spi_block_offset, width=14).pack(side='left', padx=5)
        ttk.Button(row3, text="擦除 4KB 块", command=self.on_spi_erase_block).pack(side='left', padx=5)

        row4 = ttk.Frame(tab)
        row4.pack(fill='x', pady=5)
        ttk.Button(row4, text="完整读取", command=self.on_spi_read_all).pack(side='left')
        ttk.Button(row4, text="保存到文件", command=self.on_spi_save_to_file).pack(side='left', padx=5)
        self.spi_progress = ttk.Progressbar(row4, length=200, mode='determinate')
        self.spi_progress.pack(side='left', padx=5)
        self.spi_progress_label = tk.StringVar(value="")
        ttk.Label(row4, textvariable=self.spi_progress_label).pack(side='left')

        # 完整写入区域
        sep = ttk.Separator(tab, orient='horizontal')
        sep.pack(fill='x', pady=8)

        row5 = ttk.Frame(tab)
        row5.pack(fill='x', pady=5)
        ttk.Label(row5, text="从文件完整写入:", font=('', 10, 'bold')).pack(side='left')

        row6 = ttk.Frame(tab)
        row6.pack(fill='x', pady=3)
        ttk.Label(row6, text="文件:").pack(side='left')
        self.spi_write_file = tk.StringVar()
        ttk.Entry(row6, textvariable=self.spi_write_file, width=40, state='readonly').pack(side='left', padx=5)
        ttk.Button(row6, text="选择文件...", command=self._on_spi_choose_file).pack(side='left')

        row7 = ttk.Frame(tab)
        row7.pack(fill='x', pady=3)
        self.spi_opt_backup = tk.BooleanVar(value=True)
        ttk.Checkbutton(row7, text="写入前备份旧数据", variable=self.spi_opt_backup).pack(side='left')
        self.spi_opt_erase = tk.BooleanVar(value=True)
        ttk.Checkbutton(row7, text="完整擦除 (否则只擦除写入区域)", variable=self.spi_opt_erase).pack(side='left', padx=15)
        self.spi_opt_verify = tk.BooleanVar(value=True)
        ttk.Checkbutton(row7, text="写入后校验", variable=self.spi_opt_verify).pack(side='left', padx=15)

        row8 = ttk.Frame(tab)
        row8.pack(fill='x', pady=5)
        ttk.Button(row8, text="完整写入", command=self.on_spi_write_all).pack(side='left')
        self.spi_write_progress = ttk.Progressbar(row8, length=200, mode='determinate')
        self.spi_write_progress.pack(side='left', padx=5)
        self.spi_write_progress_label = tk.StringVar(value="")
        ttk.Label(row8, textvariable=self.spi_write_progress_label).pack(side='left')

        # 读取结果显示
        self.spi_result = scrolledtext.ScrolledText(tab, height=5, font=('Consolas', 9))
        self.spi_result.pack(fill='both', expand=True, pady=5)

        # 完整读取的数据缓存及芯片大小
        self._spi_full_data = None
        self._spi_chip_size = None

    def on_spi_id(self):
        if not self._check_conn():
            return
        id_bytes, err = self.proto.spi_get_id()
        if err:
            self.log(f"SPI ID 读取失败: {err}")
            self.spi_id_result.set(f"失败: {err}")
            self._spi_chip_size = None
        else:
            hex_str = ' '.join(f'0x{b:02X}' for b in id_bytes)
            self.log(f"SPI Flash ID: {hex_str}")
            chip_name, chip_size = lookup_spi_chip(id_bytes)
            if chip_name and chip_size:
                size_mb = chip_size / (1024 * 1024)
                display = f"{chip_name} ({size_mb:.0f}MB) [{hex_str}]"
                self._spi_chip_size = chip_size
                self.log(f"识别芯片: {chip_name}, 容量: {size_mb:.0f}MB ({chip_size} bytes)")
            elif chip_name:
                display = f"{chip_name} [{hex_str}]"
                self._spi_chip_size = None
                self.log(f"未识别的芯片: {chip_name}，完整读取需要手动指定大小")
            else:
                display = f"[{hex_str}]"
                self._spi_chip_size = None
            self.spi_id_result.set(display)

    def on_spi_read(self):
        if not self._check_conn():
            return
        offset = int(self.spi_offset.get(), 16)
        length = self.spi_length.get()
        data, err = self.proto.spi_read(offset, length)
        if err:
            self.log(f"SPI 读取失败: {err}")
        else:
            self.log(f"SPI 读取 0x{offset:08X} ({length} bytes)")
            self.spi_result.delete('1.0', 'end')
            self.spi_result.insert('1.0', hexdump(data))

    def on_spi_write(self):
        if not self._check_conn():
            return
        offset = int(self.spi_offset.get(), 16)
        hex_str = self.spi_write_data.get().replace(' ', '').replace(',', '')
        data = bytes.fromhex(hex_str)
        self.log(f"SPI 写入 0x{offset:08X} ({len(data)} bytes)...")

        def _do():
            _, err = self.proto.spi_write(offset, data)
            if err:
                self.root.after(0, lambda: self.log(f"SPI 写入失败: {err}"))
                return
            _, err = self.proto.spi_wait_ready(timeout_ms=5000)
            if err:
                self.root.after(0, lambda: self.log(f"SPI 写入等待超时: {err}"))
            else:
                self.root.after(0, lambda: self.log(f"SPI 写入 0x{offset:08X} ({len(data)} bytes) → 完成"))

        threading.Thread(target=_do, daemon=True).start()

    def on_spi_erase_chip(self):
        if not self._check_conn():
            return
        self.log("SPI 全片擦除中...")

        def _do():
            _, err = self.proto.spi_erase_chip()
            if err:
                self.root.after(0, lambda: self.log(f"SPI 全片擦除失败: {err}"))
                return
            _, err = self.proto.spi_wait_ready(timeout_ms=120000)
            if err:
                self.root.after(0, lambda: self.log(f"SPI 全片擦除等待超时: {err}"))
            else:
                self.root.after(0, lambda: self.log("SPI 全片擦除完成"))

        threading.Thread(target=_do, daemon=True).start()

    def on_spi_erase_block(self):
        if not self._check_conn():
            return
        offset = int(self.spi_block_offset.get(), 16)
        self.log(f"SPI 擦除 4KB 块 0x{offset:08X}...")

        def _do():
            _, err = self.proto.spi_erase_block(offset)
            if err:
                self.root.after(0, lambda: self.log(f"SPI 块擦除失败: {err}"))
                return
            _, err = self.proto.spi_wait_ready(timeout_ms=5000)
            if err:
                self.root.after(0, lambda: self.log(f"SPI 块擦除等待超时: {err}"))
            else:
                self.root.after(0, lambda: self.log(f"SPI 块擦除 0x{offset:08X} → 完成"))

        threading.Thread(target=_do, daemon=True).start()

    def on_spi_read_all(self):
        if not self._check_conn():
            return
        # 先读取 ID 确定芯片大小
        if self._spi_chip_size is None:
            id_bytes, err = self.proto.spi_get_id()
            if err:
                self.log(f"SPI ID 读取失败: {err}")
                return
            chip_name, chip_size = lookup_spi_chip(id_bytes)
            if not chip_size:
                hex_str = ' '.join(f'0x{b:02X}' for b in id_bytes)
                self.log(f"未识别的芯片 ID: {hex_str}，无法自动确定大小。请先点击'读取 Flash ID'确认芯片。")
                return
            self._spi_chip_size = chip_size
            if chip_name:
                size_mb = chip_size / (1024 * 1024)
                self.spi_id_result.set(f"{chip_name} ({size_mb:.0f}MB)")
                self.log(f"识别芯片: {chip_name}, 容量: {size_mb:.0f}MB")

        total = self._spi_chip_size
        self.spi_progress['value'] = 0
        self.spi_progress['maximum'] = total
        self.spi_progress_label.set("0%")
        self.log(f"SPI Flash 完整读取开始 (0x00000000~0x{total:08X}, {total} bytes)")
        threading.Thread(target=self._spi_read_all_thread, args=(total,), daemon=True).start()

    def _spi_read_all_thread(self, total):
        chunk_size = 51
        buf = bytearray()
        for offset in range(0, total, chunk_size):
            n = min(chunk_size, total - offset)
            data, err = self.proto.spi_read(offset, n)
            if err:
                self.root.after(0, self._spi_read_all_done, None, f"读取失败 @ 0x{offset:08X}: {err}")
                return
            buf.extend(data)
            pct = len(buf) * 100 // total
            self.root.after(0, self._spi_read_all_progress, total, pct)
        self.root.after(0, self._spi_read_all_done, bytes(buf), None)

    def _spi_read_all_progress(self, total, pct):
        self.spi_progress['value'] = total * pct // 100
        self.spi_progress_label.set(f"{pct}%")

    def _spi_read_all_done(self, data, err):
        if err:
            self.log(f"SPI {err}")
            self.spi_progress_label.set("失败")
            return
        self._spi_full_data = data
        self.spi_progress['value'] = self._spi_chip_size
        self.spi_progress_label.set("完成")
        self.log(f"SPI Flash 完整读取完成 ({len(data)} bytes)")
        self.spi_result.delete('1.0', 'end')
        preview = min(4096, len(data))
        self.spi_result.insert('1.0', hexdump(data[:preview]))
        if len(data) > preview:
            self.spi_result.insert('end', f"\n... 仅显示前 {preview} bytes (共 {len(data)} bytes)")

    def on_spi_save_to_file(self):
        if self._spi_full_data is None:
            self.log("请先执行完整读取")
            return
        path = filedialog.asksaveasfilename(
            defaultextension=".bin",
            filetypes=[("Binary", "*.bin"), ("All Files", "*.*")],
            title="保存 SPI Flash 数据"
        )
        if path:
            with open(path, 'wb') as f:
                f.write(self._spi_full_data)
            self.log(f"SPI Flash 数据已保存到: {path} ({len(self._spi_full_data)} bytes)")

    def _ensure_spi_chip_size(self):
        """确保已获取芯片大小，返回 chip_size 或 None。"""
        if self._spi_chip_size is not None:
            return self._spi_chip_size
        id_bytes, err = self.proto.spi_get_id()
        if err:
            self.log(f"SPI ID 读取失败: {err}")
            return None
        chip_name, chip_size = lookup_spi_chip(id_bytes)
        if not chip_size:
            hex_str = ' '.join(f'0x{b:02X}' for b in id_bytes)
            self.log(f"未识别的芯片 ID: {hex_str}，请先点击'读取 Flash ID'确认芯片。")
            return None
        self._spi_chip_size = chip_size
        if chip_name:
            size_mb = chip_size / (1024 * 1024)
            self.spi_id_result.set(f"{chip_name} ({size_mb:.0f}MB)")
            self.log(f"识别芯片: {chip_name}, 容量: {size_mb:.0f}MB")
        return chip_size

    def _on_spi_choose_file(self):
        path = filedialog.askopenfilename(
            filetypes=[("Binary", "*.bin"), ("All Files", "*.*")],
            title="选择 SPI Flash 镜像文件"
        )
        if not path:
            return
        import os
        fsize = os.path.getsize(path)
        # 需要先确定芯片大小
        if not self._check_conn():
            return
        chip_size = self._ensure_spi_chip_size()
        if chip_size is None:
            return
        if fsize > chip_size:
            self.log(f"文件大小 ({fsize} bytes = {fsize / 1024 / 1024:.2f}MB) 超过芯片容量 ({chip_size} bytes)，操作取消")
            tk.messagebox.showwarning("文件过大",
                f"文件大小: {fsize} bytes ({fsize / 1024 / 1024:.2f}MB)\n"
                f"芯片容量: {chip_size} bytes ({chip_size / 1024 / 1024:.0f}MB)\n\n"
                f"文件超出容量 {fsize - chip_size} bytes，无法写入。")
            return
        self.spi_write_file.set(path)
        self.log(f"已选择文件: {path} ({fsize} bytes)")

    def on_spi_write_all(self):
        if not self._check_conn():
            return
        path = self.spi_write_file.get()
        if not path:
            self.log("请先选择文件")
            return
        chip_size = self._ensure_spi_chip_size()
        if chip_size is None:
            return
        with open(path, 'rb') as f:
            file_data = f.read()
        if len(file_data) > chip_size:
            self.log(f"文件过大 ({len(file_data)} > {chip_size})，取消写入")
            return
        if len(file_data) == 0:
            self.log("文件为空，取消写入")
            return
        # 对齐到 4KB (SPI Flash 最小擦除单位)
        padded = file_data + b'\xFF' * ((4096 - len(file_data) % 4096) % 4096)
        do_backup = self.spi_opt_backup.get()
        do_erase = self.spi_opt_erase.get()
        do_verify = self.spi_opt_verify.get()
        self.spi_write_progress['value'] = 0
        self.spi_write_progress['maximum'] = 100
        self.spi_write_progress_label.set("0%")
        self.log(f"SPI Flash 完整写入开始: {len(file_data)} bytes (填充至 {len(padded)} bytes)")
        threading.Thread(target=self._spi_write_all_thread,
                         args=(padded, chip_size, do_backup, do_erase, do_verify),
                         daemon=True).start()

    def _spi_write_all_thread(self, data, _chip_size, do_backup, do_erase, do_verify):
        total = len(data)
        page_size = 256
        erase_block_size = 4096

        # Step 1: 备份
        if do_backup:
            self.root.after(0, self._spi_write_progress_update, 0, "备份中...")
            backup = bytearray()
            read_chunk = 52
            for offset in range(0, total, read_chunk):
                n = min(read_chunk, total - offset)
                chunk, err = self.proto.spi_read(offset, n)
                if err:
                    self.root.after(0, self._spi_write_all_done, False,
                                    f"备份读取失败 @ 0x{offset:08X}: {err}")
                    return
                backup.extend(chunk)
                pct = (offset + n) * 10 // total
                self.root.after(0, self._spi_write_progress_update, pct, None)
            # 备份数据保存到本地
            self._spi_save_backup(bytes(backup))

        # Step 2: 擦除
        if do_erase:
            self.root.after(0, self._spi_write_progress_update, 10, "擦除中...")
            erase_blocks = set()
            for offset in range(0, total, erase_block_size):
                erase_blocks.add(offset & ~(erase_block_size - 1))
            blocks = sorted(erase_blocks)
            for i, block_base in enumerate(blocks):
                _, err = self.proto.spi_erase_block(block_base)
                if err:
                    self.root.after(0, self._spi_write_all_done, False,
                                    f"擦除失败 @ 0x{block_base:08X}: {err}")
                    return
                # 等待擦除完成（每块最多3秒）
                _, err = self.proto.spi_wait_ready(timeout_ms=5000)
                if err:
                    self.root.after(0, self._spi_write_all_done, False,
                                    f"擦除等待失败 @ 0x{block_base:08X}: {err}")
                    return
                pct = 10 + (i + 1) * 10 // len(blocks)
                self.root.after(0, self._spi_write_progress_update, pct, None)

        # Step 3: 写入（每包51字节，跨页时等待上一页编程完成）
        self.root.after(0, self._spi_write_progress_update, 20, "写入中...")
        spi_chunk = 51
        last_page = -1
        for offset in range(0, total, spi_chunk):
            n = min(spi_chunk, total - offset)
            # 当前包的起始字节所在页
            cur_page = offset // page_size
            # 跨页：等待上一页编程完成
            if cur_page != last_page and last_page >= 0:
                _, err = self.proto.spi_wait_ready(timeout_ms=5000)
                if err:
                    self.root.after(0, self._spi_write_all_done, False,
                                    f"写入等待失败 @ 0x{offset:08X}: {err}")
                    return
            last_page = cur_page
            chunk = data[offset:offset + n]
            _, err = self.proto.spi_write(offset, chunk)
            if err:
                self.root.after(0, self._spi_write_all_done, False,
                                f"写入失败 @ 0x{offset:08X}: {err}")
                return
            pct = 20 + (offset + n) * 50 // total
            self.root.after(0, self._spi_write_progress_update, pct, None)
        # 最后一页写完后等待
        _, err = self.proto.spi_wait_ready(timeout_ms=5000)
        if err:
            self.root.after(0, self._spi_write_all_done, False,
                            f"写入等待失败: {err}")
            return

        # Step 4: 校验
        if do_verify:
            self.root.after(0, self._spi_write_progress_update, 70, "校验中...")
            read_chunk = 52
            for offset in range(0, total, read_chunk):
                n = min(read_chunk, total - offset)
                chunk, err = self.proto.spi_read(offset, n)
                if err:
                    self.root.after(0, self._spi_write_all_done, False,
                                    f"校验读取失败 @ 0x{offset:08X}: {err}")
                    return
                if chunk != data[offset:offset + n]:
                    self.root.after(0, self._spi_write_all_done, False,
                                    f"校验失败 @ 0x{offset:08X}: 数据不匹配")
                    return
                pct = 70 + (offset + n) * 30 // total
                self.root.after(0, self._spi_write_progress_update, pct, None)

        self.root.after(0, self._spi_write_all_done, True, None)

    def _spi_write_progress_update(self, pct, msg):
        if pct is not None:
            self.spi_write_progress['value'] = pct
        if msg:
            self.spi_write_progress_label.set(msg)
        else:
            self.spi_write_progress_label.set(f"{pct}%")

    def _spi_write_all_done(self, _success, err):
        if err:
            self.log(f"SPI Flash 完整写入失败: {err}")
            self.spi_write_progress_label.set("失败")
            return
        self.spi_write_progress['value'] = 100
        self.spi_write_progress_label.set("完成")
        self.log("SPI Flash 完整写入成功")

    def _spi_save_backup(self, backup_data):
        import os
        from datetime import datetime
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        save_dir = os.path.dirname(os.path.abspath(__file__))
        path = os.path.join(save_dir, f"spiflash_backup_{ts}.bin")
        with open(path, 'wb') as f:
            f.write(backup_data)
        self.log(f"SPI Flash 备份已保存: {path} ({len(backup_data)} bytes)")

    # ─── Tab: FATFS ───
    def _build_tab_fatfs(self):
        tab = ttk.Frame(self.notebook, padding=10)
        self.notebook.add(tab, text="FATFS 文件系统")

        # 第一行: 检查 / 格式化
        row0 = ttk.Frame(tab)
        row0.pack(fill='x', pady=5)
        ttk.Button(row0, text="检查格式化状态", command=self.on_fatfs_check).pack(side='left')
        self.fatfs_check_result = tk.StringVar()
        ttk.Label(row0, textvariable=self.fatfs_check_result).pack(side='left', padx=10)

        row0b = ttk.Frame(tab)
        row0b.pack(fill='x', pady=5)
        ttk.Button(row0b, text="格式化 (FAT)", command=self.on_fatfs_format).pack(side='left')
        ttk.Button(row0b, text="格式化 (需确认)", command=self.on_fatfs_format_confirm).pack(side='left', padx=5)

        # 第二行: 文件系统信息
        row1 = ttk.Frame(tab)
        row1.pack(fill='x', pady=5)
        ttk.Button(row1, text="获取文件系统信息", command=self.on_fatfs_get_fs_info).pack(side='left')
        self.fatfs_fs_info = tk.StringVar()
        ttk.Label(row1, textvariable=self.fatfs_fs_info).pack(side='left', padx=10)

        # 分隔
        sep = ttk.Separator(tab, orient='horizontal')
        sep.pack(fill='x', pady=8)

        # 文件列表区域
        ttk.Label(tab, text="文件操作:", font=('', 10, 'bold')).pack(anchor='w')

        row2 = ttk.Frame(tab)
        row2.pack(fill='x', pady=5)
        ttk.Button(row2, text="扫描文件", command=self.on_fatfs_scan).pack(side='left')
        self.fatfs_file_count = tk.StringVar()
        ttk.Label(row2, textvariable=self.fatfs_file_count).pack(side='left', padx=10)

        # 文件列表
        list_frame = ttk.Frame(tab)
        list_frame.pack(fill='both', expand=True, pady=5)

        scrollbar = ttk.Scrollbar(list_frame)
        scrollbar.pack(side='right', fill='y')

        columns = ('name', 'size', 'chunks')
        self.fatfs_tree = ttk.Treeview(list_frame, columns=columns, show='headings', height=6,
                                        yscrollcommand=scrollbar.set)
        self.fatfs_tree.heading('name', text='文件名')
        self.fatfs_tree.heading('size', text='大小 (bytes)')
        self.fatfs_tree.heading('chunks', text='块数')
        self.fatfs_tree.column('name', width=250)
        self.fatfs_tree.column('size', width=120)
        self.fatfs_tree.column('chunks', width=80)
        self.fatfs_tree.pack(side='left', fill='both', expand=True)
        scrollbar.config(command=self.fatfs_tree.yview)

        # 文件操作按钮
        row3 = ttk.Frame(tab)
        row3.pack(fill='x', pady=5)
        ttk.Button(row3, text="读取选中文件", command=self.on_fatfs_read_selected).pack(side='left')
        ttk.Button(row3, text="保存选中文件", command=self.on_fatfs_save_selected).pack(side='left', padx=5)

        # 写入新文件
        sep2 = ttk.Separator(tab, orient='horizontal')
        sep2.pack(fill='x', pady=8)
        ttk.Label(tab, text="上传文件:", font=('', 10, 'bold')).pack(anchor='w')

        row4 = ttk.Frame(tab)
        row4.pack(fill='x', pady=3)
        ttk.Label(row4, text="本地文件:").pack(side='left')
        self.fatfs_upload_file = tk.StringVar()
        ttk.Entry(row4, textvariable=self.fatfs_upload_file, width=40, state='readonly').pack(side='left', padx=5)
        ttk.Button(row4, text="选择文件...", command=self._on_fatfs_choose_file).pack(side='left')

        row5 = ttk.Frame(tab)
        row5.pack(fill='x', pady=3)
        ttk.Label(row5, text="远程文件名:").pack(side='left')
        self.fatfs_remote_name = tk.StringVar(value="display_xxx")
        ttk.Entry(row5, textvariable=self.fatfs_remote_name, width=32).pack(side='left', padx=5)
        ttk.Button(row5, text="上传", command=self.on_fatfs_upload).pack(side='left', padx=5)
        self.fatfs_upload_progress = ttk.Progressbar(row5, length=200, mode='determinate')
        self.fatfs_upload_progress.pack(side='left', padx=5)
        self.fatfs_upload_label = tk.StringVar(value="")
        ttk.Label(row5, textvariable=self.fatfs_upload_label).pack(side='left')

        # 内部缓存
        self._fatfs_file_cache = []  # [(name, size, chunks), ...]

    def on_fatfs_check(self):
        if not self._check_conn():
            return
        status, err = self.proto.fatfs_check_formatted()
        if err:
            self.log(f"FATFS 检查失败: {err}")
            self.fatfs_check_result.set(f"失败: {err}")
        else:
            if status == 0:
                self.log("SPI Flash 已格式化 (FAT)")
                self.fatfs_check_result.set("已格式化")
            else:
                self.log("SPI Flash 未格式化")
                self.fatfs_check_result.set("未格式化")

    def on_fatfs_format(self):
        if not self._check_conn():
            return
        self.log("正在格式化 SPI Flash (FAT)...")
        status, err = self.proto.fatfs_format()
        if err:
            self.log(f"格式化失败: {err}")
        elif status == 0:
            self.log("格式化成功")
        else:
            self.log(f"格式化失败, 状态码: {status}")

    def on_fatfs_format_confirm(self):
        if not self._check_conn():
            return
        import tkinter.messagebox as mb
        if mb.askyesno("确认格式化",
                        "将把 SPI Flash 格式化为 FAT 文件系统，所有数据将被清除。\n\n确认继续？"):
            self.on_fatfs_format()

    def on_fatfs_get_fs_info(self):
        if not self._check_conn():
            return
        info, err = self.proto.fatfs_get_fs_info()
        if err:
            self.log(f"获取文件系统信息失败: {err}")
            self.fatfs_fs_info.set(f"失败: {err}")
        else:
            total_mb = info['total'] / (1024 * 1024)
            free_mb = info['free'] / (1024 * 1024)
            used_mb = total_mb - free_mb
            self.log(f"FATFS: 总容量 {total_mb:.2f}MB, 已用 {used_mb:.2f}MB, 空闲 {free_mb:.2f}MB")
            self.fatfs_fs_info.set(f"总容量: {total_mb:.2f}MB | 已用: {used_mb:.2f}MB | 空闲: {free_mb:.2f}MB")

    def on_fatfs_scan(self):
        if not self._check_conn():
            return
        count, err = self.proto.fatfs_scan_files()
        if err:
            self.log(f"文件扫描失败: {err}")
            self.fatfs_file_count.set(f"失败: {err}")
            return

        self.log(f"扫描到 {count} 个文件")
        self.fatfs_file_count.set(f"{count} 个文件")

        # 清空列表
        for item in self.fatfs_tree.get_children():
            self.fatfs_tree.delete(item)
        self._fatfs_file_cache.clear()

        # 获取每个文件的信息
        for i in range(count):
            info, err = self.proto.fatfs_get_file_info(i)
            if err:
                self.log(f"获取文件 {i} 信息失败: {err}")
                continue
            self._fatfs_file_cache.append(info)
            self.fatfs_tree.insert('', 'end', values=(info['name'], info['size'], info['chunks']))

    def _get_selected_fatfs_file(self):
        sel = self.fatfs_tree.selection()
        if not sel:
            self.log("请先选择一个文件")
            return None, None
        idx = self.fatfs_tree.index(sel[0])
        if idx >= len(self._fatfs_file_cache):
            self.log("选中索引无效")
            return None, None
        return idx, self._fatfs_file_cache[idx]

    def on_fatfs_read_selected(self):
        if not self._check_conn():
            return
        idx, info = self._get_selected_fatfs_file()
        if info is None:
            return
        total_chunks = info['chunks']
        self.log(f"读取文件: {info['name']} ({info['size']} bytes, {total_chunks} chunks)")

        buf = bytearray()
        for ci in range(total_chunks):
            chunk, err = self.proto.fatfs_read_file(idx, ci)
            if err:
                self.log(f"读取 chunk {ci} 失败: {err}")
                return
            buf.extend(chunk)

        self.log(f"文件读取完成: {info['name']} ({len(buf)} bytes)")
        # 显示前 2KB 内容
        preview = min(2048, len(buf))
        self.log(f"前 {preview} bytes:\n{hexdump(buf[:preview])}")

    def on_fatfs_save_selected(self):
        if not self._check_conn():
            return
        idx, info = self._get_selected_fatfs_file()
        if info is None:
            return
        path = filedialog.asksaveasfilename(
            defaultextension="",
            initialfile=info['name'],
            filetypes=[("All Files", "*.*")],
            title="保存文件"
        )
        if not path:
            return

        total_chunks = info['chunks']
        self.log(f"保存文件: {info['name']} → {path}")

        buf = bytearray()
        for ci in range(total_chunks):
            chunk, err = self.proto.fatfs_read_file(idx, ci)
            if err:
                self.log(f"读取 chunk {ci} 失败: {err}")
                return
            buf.extend(chunk)

        with open(path, 'wb') as f:
            f.write(buf)
        self.log(f"文件已保存: {path} ({len(buf)} bytes)")

    def _on_fatfs_choose_file(self):
        path = filedialog.askopenfilename(
            filetypes=[("All Files", "*.*")],
            title="选择要上传的文件"
        )
        if path:
            import os
            fsize = os.path.getsize(path)
            self.fatfs_upload_file.set(path)
            self.log(f"已选择文件: {path} ({fsize} bytes)")

    def on_fatfs_upload(self):
        if not self._check_conn():
            return
        path = self.fatfs_upload_file.get()
        if not path:
            self.log("请先选择本地文件")
            return
        remote_name = self.fatfs_remote_name.get().strip()
        if not remote_name:
            self.log("请输入远程文件名")
            return

        with open(path, 'rb') as f:
            file_data = f.read()

        if len(file_data) == 0:
            self.log("文件为空")
            return

        chunk_size = FATFS_WRITE_CHUNK
        total_chunks = (len(file_data) + chunk_size - 1) // chunk_size

        self.fatfs_upload_progress['value'] = 0
        self.fatfs_upload_progress['maximum'] = 100
        self.fatfs_upload_label.set("0%")

        self.log(f"上传文件: {remote_name} ({len(file_data)} bytes)")
        threading.Thread(target=self._fatfs_upload_thread,
                         args=(remote_name, file_data, total_chunks),
                         daemon=True).start()

    def _fatfs_upload_thread(self, remote_name, file_data, _total_chunks):
        chunk_size = FATFS_WRITE_CHUNK

        # Step 1: 创建文件
        self.root.after(0, self._fatfs_upload_update, 0, "创建文件...")
        status, err = self.proto.fatfs_create_file(remote_name, len(file_data))
        if err:
            self.root.after(0, self._fatfs_upload_done, False, f"创建文件失败: {err}")
            return
        if status != 0:
            self.root.after(0, self._fatfs_upload_done, False, f"创建文件失败, 状态码: {status}")
            return
        self.root.after(0, self.log, f"文件已创建: {remote_name}")

        # Step 2: 重新扫描获取文件索引
        self.root.after(0, self._fatfs_upload_update, 5, "扫描文件...")
        count, err = self.proto.fatfs_scan_files()
        if err:
            self.root.after(0, self._fatfs_upload_done, False, f"扫描失败: {err}")
            return

        # 找到刚创建的文件索引
        file_index = None
        for i in range(count):
            info, err = self.proto.fatfs_get_file_info(i)
            if err:
                continue
            if info['name'] == remote_name:
                file_index = i
                break
        if file_index is None:
            self.root.after(0, self._fatfs_upload_done, False, f"找不到刚创建的文件: {remote_name}")
            return

        # Step 3: 分块写入
        for offset in range(0, len(file_data), chunk_size):
            n = min(chunk_size, len(file_data) - offset)
            chunk = file_data[offset:offset + n]
            status, err = self.proto.fatfs_write_file(file_index, offset, chunk)
            if err:
                self.root.after(0, self._fatfs_upload_done, False,
                                f"写入失败 @ offset={offset}: {err}")
                return
            if status != 0:
                self.root.after(0, self._fatfs_upload_done, False,
                                f"写入失败 @ offset={offset}, 状态码: {status}")
                return
            pct = (offset + n) * 100 // len(file_data)
            self.root.after(0, self._fatfs_upload_update, pct, None)

        self.root.after(0, self._fatfs_upload_done, True, None)

    def _fatfs_upload_update(self, pct, msg):
        if pct is not None:
            self.fatfs_upload_progress['value'] = pct
        if msg:
            self.fatfs_upload_label.set(msg)
        else:
            self.fatfs_upload_label.set(f"{pct}%")

    def _fatfs_upload_done(self, _success, err):
        if err:
            self.log(f"FATFS 上传失败: {err}")
            self.fatfs_upload_label.set("失败")
            return
        self.fatfs_upload_progress['value'] = 100
        self.fatfs_upload_label.set("完成")
        self.log("FATFS 文件上传成功")
        # 自动刷新文件列表
        self.on_fatfs_scan()

    def run(self):
        self.root.mainloop()
        if self.proto:
            self.proto.close()


def main():
    app = HIDTesterGUI()
    app.run()


if __name__ == '__main__':
    main()
