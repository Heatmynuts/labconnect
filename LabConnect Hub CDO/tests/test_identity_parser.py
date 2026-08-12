import pathlib
import subprocess
import tempfile
import textwrap
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
HEADER_DIR = ROOT / "firmware" / "atom-lite-cdo" / "LabConnectHubCDO"


class IdentityParserTest(unittest.TestCase):
    def test_realistic_identity_replies_and_allow_list(self):
        source = textwrap.dedent(
            r'''
            #include <cassert>
            #include <cstring>
            #include "CdoIdentity.h"

            void expect(const char *reply, const char *expected) {
              char output[6] = {};
              const auto *balance = extractCdoBalance(reply, std::strlen(reply), output);
              assert(balance != nullptr);
              assert(std::strcmp(output, expected) == 0);
              assert(std::strcmp(balance->id, expected) == 0);
            }

            void reject(const char *reply) {
              char output[6] = {};
              assert(extractCdoBalance(reply, std::strlen(reply), output) == nullptr);
            }

            int main() {
              expect("I10 A \"CDO04\"\r\n", "CDO04");
              expect("I10 A \"CDO06\"\r\n", "CDO06");
              expect("   CDO03   \r\n", "CDO03");
              expect("ID,CDO05\r\n", "CDO05");
              expect("cdo-02\r\n", "CDO02");
              expect("ID,000003\r\n", "CDO03");
              expect("000005\r\n", "CDO05");
              expect("ID,0000003\r\n", "CDO03");
              expect("ID,00000000000005\r\n", "CDO05");

              const char *part1 = "I10 A \"CD";
              const char *part2 = "O04\"\r\n";
              char fragmented[32] = {};
              std::strcpy(fragmented, part1);
              std::strcat(fragmented, part2);
              expect(fragmented, "CDO04");

              reject("CDO01\r\n");
              reject("CDO07\r\n");
              reject("000001\r\n");
              reject("000007\r\n");
              reject("0000030\r\n");
              reject("ID,0000000\r\n");
              reject("S    0.003 g\r\n");
              reject("CDO04X\r\n");
              reject("I10 A \"LAB04\"\r\n");

              assert(findCdoBalance("CDO02")->manufacturer == CdoManufacturer::AD);
              assert(findCdoBalance("CDO04")->manufacturer == CdoManufacturer::Mettler);
              assert(findCdoBalance("CDO06")->desiredComPort == 6);
              return 0;
            }
            '''
        )

        with tempfile.TemporaryDirectory() as temporary:
            temporary_path = pathlib.Path(temporary)
            cpp = temporary_path / "identity_test.cpp"
            executable = temporary_path / "identity_test"
            cpp.write_text(source, encoding="utf-8")
            subprocess.run(
                [
                    "clang++",
                    "-std=c++11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(HEADER_DIR),
                    str(cpp),
                    "-o",
                    str(executable),
                ],
                check=True,
            )
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
