"""Shipped UI messages have usable translations and retain Qt substitutions."""

from pathlib import Path
import re
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
PLURALS = {"en": 2, "de": 2, "es": 2, "fr": 2, "it": 2, "ja": 1, "pl": 3, "pt_BR": 2, "ru": 3, "zh_CN": 1}


class TranslationCatalogTests(unittest.TestCase):
    def test_active_messages_are_complete_and_preserve_qt_placeholders(self):
        for path in sorted((ROOT / "resources/i18n").glob("*.ts")):
            catalog = ET.parse(path).getroot()
            language = catalog.attrib["language"]
            for context in catalog.findall("context"):
                for message in context.findall("message"):
                    source = message.findtext("source", "")
                    translation = message.find("translation")
                    if translation is not None and translation.get("type") in {"vanished", "obsolete"}:
                        continue
                    with self.subTest(language=language, context=context.findtext("name"), source=source):
                        self.assertIsNotNone(translation)
                        self.assertNotEqual("unfinished", translation.get("type"))
                        forms = translation.findall("numerusform") if message.get("numerus") == "yes" else [translation]
                        self.assertEqual(PLURALS[language] if message.get("numerus") == "yes" else 1, len(forms))
                        for form in forms:
                            text = "".join(form.itertext())
                            self.assertTrue(text.strip())
                            self.assertEqual(
                                set(re.findall(r"%L?(?:\d+|n)", source)), set(re.findall(r"%L?(?:\d+|n)", text))
                            )


if __name__ == "__main__":
    unittest.main()
