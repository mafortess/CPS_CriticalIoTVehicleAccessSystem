import re
import threading
from typing import Dict, Tuple, List, Optional


# Default identifier ranges for sensor categories (inclusive)
# Adjust as your network evolves.
SENSOR_ID_RANGES: Dict[str, Tuple[int, int]] = {
	"temperature": (0x030, 0x03F),
	"air_quality": (0x040, 0x04F),
	"humidity": (0x050, 0x05F),
}


class SerialCANReceiver:
	"""Simple CAN-over-Serial line reader.

	Reads ASCII lines from a serial port. Attempts to parse lines containing
	CAN fields: identifier (ID), DLC (data length), and data bytes.

	Supported line formats (flexible, case-insensitive):
	- "ID=0x036 DLC=2 DATA=00 FF"
	- "ID: 54 DLC: 8 DATA: AA BB CC DD EE FF 00 11"
	- "ID 0x321 DLC 2 DATA 01, 02, 03"

	If a line can't be parsed, it will be printed verbatim.
	"""

	def __init__(
		self,
		port: str,
		baudrate: int = 115200,
		timeout: float = 1.0,
		sensor_id_ranges: Optional[Dict[str, Tuple[int, int]]] = None,
	) -> None:

		self.port = port
		self.baudrate = baudrate
		self.timeout = timeout
		self.sensor_id_ranges = sensor_id_ranges or SENSOR_ID_RANGES
		self._stop_evt = threading.Event()
		self._thread: Optional[threading.Thread] = None
		# Use a generic type to avoid import-time typing issues if pyserial isn't installed yet
		self._ser: Optional[object] = None

		# Precompile regexes for performance and flexibility
		# Matches ID=0x123 or ID: 291 or ID 0x123
		self._id_re = re.compile(r"\bID\s*[:=]?\s*(0x[0-9A-Fa-f]+|\d+)\b", re.IGNORECASE)
		self._dlc_re = re.compile(r"\bDLC\s*[:=]?\s*(\d+)\b", re.IGNORECASE)
		# DATA section: bytes separated by spaces or commas, hex preferred but decimal allowed
		self._data_re = re.compile(r"\bDATA\s*[:=]?\s*([0-9A-Fa-f\s,]+)\b", re.IGNORECASE)

	def start(self) -> None:
		if self._thread and self._thread.is_alive():
			return
		self._stop_evt.clear()
		# Import pyserial lazily to avoid workspace analysis errors when not installed yet
		try:
			import serial as _serial  # type: ignore
		except ImportError as exc:  # pragma: no cover
			raise RuntimeError(
				"pyserial not installed. Please add 'pyserial' to requirements and install."
			) from exc
		self._ser = _serial.Serial(self.port, self.baudrate, timeout=self.timeout)
		self._thread = threading.Thread(target=self._run, name="SerialCANReceiver", daemon=True)
		self._thread.start()
		print(f"[Serial] Listening on {self.port} @ {self.baudrate} baud")

	def stop(self) -> None:
		self._stop_evt.set()
		if self._thread:
			self._thread.join(timeout=2.0)
		if self._ser:
			try:
				self._ser.close()
			except Exception:
				pass

	def _run(self) -> None:
		assert self._ser is not None
		while not self._stop_evt.is_set():
			try:
				raw = self._ser.readline()
				if not raw:
					continue
				line = raw.decode(errors="replace").strip()
			except Exception as exc:
				print(f"[Serial] Read error: {exc}")
				break

			parsed = self._parse_can_line(line)
			if parsed is None:
				# Not a CAN-formatted line, print as-is for visibility
				print(f"[Serial] {line}")
				continue

			can_id, dlc, data_bytes = parsed
			category = self._categorize(can_id)
			id_str = f"0x{can_id:03X}"
			data_hex = " ".join(f"{b:02X}" for b in data_bytes)
			print(
				f"[CAN] ID={id_str} category={category} DLC={dlc} DATA=[{data_hex}]"
			)

	def _parse_can_line(self, line: str) -> Optional[Tuple[int, int, List[int]]]:
		"""Attempt to parse a CAN line into (id, dlc, data_bytes)."""
		id_match = self._id_re.search(line)
		dlc_match = self._dlc_re.search(line)
		data_match = self._data_re.search(line)

		if not (id_match and dlc_match and data_match):
			return None

		# Parse ID (hex or decimal)
		id_str = id_match.group(1)
		try:
			can_id = int(id_str, 16) if id_str.lower().startswith("0x") else int(id_str)
		except ValueError:
			return None

		# Parse DLC
		try:
			dlc = int(dlc_match.group(1))
		except ValueError:
			return None

		# Parse DATA bytes (space/comma separated, hex preferred)
		data_field = data_match.group(1)
		tokens = [t for t in re.split(r"[\s,]+", data_field.strip()) if t]
		data_bytes: List[int] = []
		for t in tokens:
			tt = t.lower()
			try:
				val = int(tt, 16) if re.fullmatch(r"0x?[0-9a-f]+", tt) else int(tt)
			except ValueError:
				# Skip non-byte tokens gracefully
				continue
			if 0 <= val <= 255:
				data_bytes.append(val)

		# Respect DLC: truncate or pad with zeros
		if dlc < len(data_bytes):
			data_bytes = data_bytes[:dlc]
		elif dlc > len(data_bytes):
			data_bytes.extend([0] * (dlc - len(data_bytes)))

		return can_id, dlc, data_bytes

	def _categorize(self, can_id: int) -> str:
		for name, (lo, hi) in self.sensor_id_ranges.items():
			if lo <= can_id <= hi:
				return name
		return "unknown"


def run_serial_can_logger(
	port: str,
	baudrate: int = 115200,
	sensor_id_ranges: Optional[Dict[str, Tuple[int, int]]] = None,
) -> SerialCANReceiver:
	"""Convenience runner: starts a background receiver that prints to terminal.

	Returns the receiver instance so callers can stop it later.
	"""
	receiver = SerialCANReceiver(
		port=port, baudrate=baudrate, sensor_id_ranges=sensor_id_ranges
	)
	receiver.start()
	return receiver


if __name__ == "__main__":
	# Quick manual run: update COM port as needed
	# Example formats accepted:
	#   ID=0x036 DLC=2 DATA=00 FF
	#   ID: 54 DLC: 8 DATA: AA BB CC DD EE FF 00 11
	import os
	com_port = os.getenv("SERIAL_PORT", "COM3")
	baud = int(os.getenv("SERIAL_BAUD", "115200"))
	print("Starting Serial CAN logger. Press Ctrl+C to stop.")
	try:
		rcv = run_serial_can_logger(com_port, baud)
		# Keep main thread alive while background thread runs
		threading.Event().wait()
	except KeyboardInterrupt:
		print("Stopping...")
		try:
			rcv.stop()
		except Exception:
			pass
