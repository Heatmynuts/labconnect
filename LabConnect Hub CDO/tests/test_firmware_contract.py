import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SKETCH_DIR = ROOT / "firmware" / "atom-lite-cdo" / "LabConnectHubCDO"
SOURCE = (SKETCH_DIR / "LabConnectHubCDO.ino").read_text(encoding="utf-8")


class FirmwareContractTest(unittest.TestCase):
    def test_uart_contract_is_common_to_all_balances(self):
        self.assertIn("BALANCE_BAUD = 9600", SOURCE)
        self.assertIn("BALANCE_SERIAL_CONFIG = SERIAL_8N1", SOURCE)
        self.assertIn('"I10\\r\\n"', SOURCE)
        self.assertIn('"?ID\\r\\n"', SOURCE)

    def test_bridge_is_byte_transparent(self):
        self.assertIn("SerialBT.write(bridgeBuffer, count)", SOURCE)
        self.assertIn("BalanceSerial.write(bridgeBuffer, count)", SOURCE)
        self.assertNotIn("normalizeWeightReply", SOURCE)
        self.assertNotIn("parseAdWeight", SOURCE)
        self.assertNotIn("dtostrf", SOURCE)

    def test_stale_balance_bytes_are_drained_without_a_client(self):
        balance_to_bt = SOURCE[
            SOURCE.index("void forwardBalanceToBluetooth()") :
            SOURCE.index("void forwardBluetoothToBalance()")
        ]
        self.assertIn("BalanceSerial.read()", balance_to_bt)
        self.assertIn("if (SerialBT.hasClient())", balance_to_bt)

    def test_web_interface_is_read_only(self):
        self.assertNotIn("HTTP_POST", SOURCE)
        self.assertNotIn('Web.on("/api/send"', SOURCE)
        self.assertNotIn('Web.on("/api/config"', SOURCE)
        self.assertIn('Web.on("/api/status", HTTP_GET', SOURCE)

    def test_button_has_no_single_click_command(self):
        self.assertNotIn("performLocalWeightTest", SOURCE)
        self.assertIn("One or two clicks deliberately have no effect", SOURCE)


if __name__ == "__main__":
    unittest.main()
