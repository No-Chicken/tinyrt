"""The public ABI and wire contract must match the production implementation."""
import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]

class ContractTests(unittest.TestCase):
    def test_authoritative_contract_is_present(self):
        self.assertTrue((ROOT / "contracts/abi-v1.json").is_file(), "core must own ABI v1 contract")

    def test_native_signatures_permissions_and_exports(self):
        path = ROOT / "contracts/abi-v1.json"
        self.assertTrue(path.is_file(), "core must own ABI v1 contract")
        spec = json.loads(path.read_text(encoding="utf-8"))
        source = (ROOT / "runtime/wasm/tinyrt_runtime.c").read_text(encoding="utf-8")
        natives = re.findall(r'\{ "(\w+)", \(void \*\)\w+, "([()i]+)", NULL \}', source)
        self.assertEqual(natives, [(x["name"], x["signature"]) for x in spec["imports"]])
        permissions = re.search(r'required_permission\[\] = \{([^}]+)', source).group(1)
        self.assertEqual([int(x.strip()) for x in permissions.split(",")], [x["permission"] for x in spec["imports"]])
        native_arity = re.search(r'arity\[\] = \{([^}]+)', source).group(1)
        self.assertEqual([int(x.strip()) for x in native_arity.split(",")], [x["signature"].split(")")[0].count("i") for x in spec["imports"]])
        exports = re.search(r'exports\[\] = \{([^}]+)', source).group(1)
        self.assertEqual(re.findall(r'"(\w+)"', exports), [x["name"] for x in spec["exports"]])
        export_arity = re.search(r'export_arity\[\] = \{([^}]+)', source).group(1)
        self.assertEqual([int(x.strip()) for x in export_arity.split(",")], [x["arity"] for x in spec["exports"]])
        header = (ROOT / "contracts/guest-v1.h").read_text(encoding="utf-8")
        self.assertEqual(re.findall(r'TINYRT_IMPORT\((\w+)\) (?:int32_t|uint32_t)', header), [x["name"] for x in spec["imports"]])
        prototypes = re.findall(r'TINYRT_IMPORT\((\w+)\) (?:int32_t|uint32_t) \w+\(([^;]*)\);', header)
        for (name, args), expected in zip(prototypes, spec["imports"]):
            arity = 0 if args.strip() == "void" else args.count(",") + 1
            self.assertEqual(arity, expected["signature"].split(")")[0].count("i"), name)
        for name, value in spec["events"].items():
            self.assertRegex(header, r"#define " + name + " " + str(value) + r"\b")

    def test_wire_constants_match_c_headers(self):
        path = ROOT / "contracts/wire-v1.json"
        self.assertTrue(path.is_file(), "core must own package/protocol contract")
        spec = json.loads(path.read_text(encoding="utf-8"))
        for relative, values in spec["headers"].items():
            source = (ROOT / relative).read_text(encoding="utf-8")
            for name, expected in values.items():
                found = re.search(r"#define " + name + r" (?:UINT32_C\()?((?:0x)?[0-9A-Fa-f]+)[uU]?", source)
                self.assertIsNotNone(found, name)
                self.assertEqual(int(found.group(1), 0), expected, name)

if __name__ == "__main__":
    unittest.main()
