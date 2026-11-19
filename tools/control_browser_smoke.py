#!/usr/bin/env python3
"""Run desktop and phone-size Chromium against the actual device. Requires Playwright and pyserial."""
import argparse
import json
from pathlib import Path
import re
import time

from playwright.sync_api import sync_playwright, expect
from serial_smoke import Device


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--host")
    parser.add_argument("--chromium", help="Path to a locally installed Chrome or Chromium")
    parser.add_argument("--output", type=Path, default=Path("build/hardware/local-browser"))
    args = parser.parse_args()
    usb = Device(args.port, args.output / "usb.log")
    initial = usb.status()
    original_pet = re.search(r"active=(\d) style=(\d) action=(\w+)", usb.command("pet status"))
    assert initial["ui"] in (0, 20) and original_pet, "Run from Home or Desktop Pet"
    host = args.host or initial["sta"]
    results = []
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(executable_path=args.chromium, args=["--no-proxy-server"])
            for name, width, height in (("desktop", 1200, 850), ("phone-size", 390, 844)):
                context = browser.new_context(viewport={"width": width, "height": height})
                page = context.new_page()
                errors = []
                page.on("pageerror", lambda error: errors.append(str(error)))
                usb.open_home_card(2)
                usb.key("up")
                code = re.search(r"PAIR CODE:\s*(\d{6})", usb.command("wifi"))
                assert code, "Network > Up did not open pairing"
                page.goto("http://" + host + "/")
                page.locator("#code").fill(code[1])
                page.get_by_role("button", name="配对并连接").click()
                page.get_by_role("button", name="像素猫").click()
                page.get_by_role("button", name="跳舞").click()
                expect(page.locator("#petState")).to_have_text("像素猫 · 跳舞")
                expect(page.locator("#screen")).to_be_visible()
                assert "style=1 action=dance" in usb.command("pet status")
                assert page.evaluate("document.documentElement.scrollWidth <= innerWidth")
                page.screenshot(path=str(args.output / (name + ".png")), full_page=True)
                page.reload()
                expect(page.locator("#screen")).to_be_visible()
                context.set_offline(True)
                expect(page.locator("#feedback")).to_contain_text("连接中断")
                context.set_offline(False)
                expect(page.locator("#screen")).to_be_visible(timeout=12000)
                page.get_by_text("显示设置", exact=True).click()
                changed = (initial["brightness"] + 1) % 4
                page.locator("#brightness").select_option(index=changed)
                expect(page.locator("#brightness")).to_be_enabled()
                assert usb.status()["brightness"] == changed
                page.locator("#brightness").select_option(index=initial["brightness"])
                page.get_by_role("button", name="退出桌宠").click()
                expect(page.locator("#screen")).to_be_hidden()
                page.get_by_role("button", name="退出配对").click()
                expect(page.locator("#pairing")).to_be_visible()
                assert not errors, errors
                context.close()
                results.append(name)
                print("PASS", name, flush=True)
            browser.close()
    finally:
        if usb.status()["brightness"] != initial["brightness"]:
            usb.apply_settings({k: initial[k] for k in ("brightness", "motion", "header")})
        usb.command("pet open " + original_pet[2])
        if original_pet[1] == "1":
            usb.command("pet action " + original_pet[3])
        else:
            usb.command("pet close")
            for _ in range((initial["selected"] - usb.status()["selected"]) % 6):
                usb.key("down")
        time.sleep(0.9)
        usb.close()
    (args.output / "results.json").write_text(json.dumps(results) + "\n")


if __name__ == "__main__":
    main()
