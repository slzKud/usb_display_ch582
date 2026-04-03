#!/usr/bin/env python3
"""CH582 USB HID 测试工具"""

import struct
import tkinter as tk
from tkinter import ttk, scrolledtext
import hid

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

# SPI actions
SPI_GET_ID = 0x00
SPI_READ = 0x01
SPI_WRITE = 0x02
SPI_ERASE_CHIP = 0x03
SPI_ERASE_BLOCK = 0x04

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
        return payload[2:6], None  # skip status + action

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

        # 读取结果显示
        self.df_result = scrolledtext.ScrolledText(tab, height=8, font=('Consolas', 9))
        self.df_result.pack(fill='both', expand=True, pady=5)

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

        # 读取结果显示
        self.spi_result = scrolledtext.ScrolledText(tab, height=8, font=('Consolas', 9))
        self.spi_result.pack(fill='both', expand=True, pady=5)

    def on_spi_id(self):
        if not self._check_conn():
            return
        id_bytes, err = self.proto.spi_get_id()
        if err:
            self.log(f"SPI ID 读取失败: {err}")
            self.spi_id_result.set(f"失败: {err}")
        else:
            hex_str = []
            for id in id_bytes:
                hex_str.append(f"{hex(id)}")
            self.log(f"SPI Flash ID: {" ".join(hex_str)}")
            self.spi_id_result.set(" ".join(hex_str))

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
        status, err = self.proto.spi_write(offset, data)
        if err:
            self.log(f"SPI 写入失败: {err}")
        else:
            self.log(f"SPI 写入 0x{offset:08X} ({len(data)} bytes) → {STATUS_NAMES.get(status, '?')}")

    def on_spi_erase_chip(self):
        if not self._check_conn():
            return
        status, err = self.proto.spi_erase_chip()
        if err:
            self.log(f"SPI 全片擦除失败: {err}")
        else:
            self.log(f"SPI 全片擦除 → {STATUS_NAMES.get(status, '?')}")

    def on_spi_erase_block(self):
        if not self._check_conn():
            return
        offset = int(self.spi_block_offset.get(), 16)
        status, err = self.proto.spi_erase_block(offset)
        if err:
            self.log(f"SPI 块擦除失败: {err}")
        else:
            self.log(f"SPI 块擦除 0x{offset:08X} → {STATUS_NAMES.get(status, '?')}")

    def run(self):
        self.root.mainloop()
        if self.proto:
            self.proto.close()


def main():
    app = HIDTesterGUI()
    app.run()


if __name__ == '__main__':
    main()
