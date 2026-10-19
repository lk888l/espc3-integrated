#!/usr/bin/env python3
"""Verify dedicated Back-button routing over USB without changing saved settings.

The console injects recognized events, not physical GPIO levels. Real debounce
and sustained presses are covered by core_tests; electrical checks are manual.
"""
import argparse
import json
from pathlib import Path
import time

from serial_smoke import Device


HOME, TOOLS, SETTINGS, GAMES, PET = 0, 1, 4, 11, 20
READY, PLAYING, PAUSED = 0, 1, 2
TAIL = ("long", "repeat", "repeat", "release", "click", "double")


def exercise(device, passed):
    def expect(view):
        state = device.status()
        assert state["ui"] == view, state
        return state

    def open_page(card, row, size):
        device.open_home_card(card)
        if row is not None:
            selected = device.status()["selected"]
            for _ in range((row - selected) % size):
                device.key("down")
            device.key("enter")

    def exit_once(view, parent):
        expect(view)
        # Back has no navigation action for non-press gestures, even without
        # a preceding Press. This also catches holds leaking across page changes.
        for gesture in TAIL:
            device.key("back", gesture)
            expect(view)
        device.key("back", "press")
        expect(parent)
        for gesture in TAIL:
            device.key("back", gesture)
            expect(parent)
        if view == 9:
            time.sleep(0.1)
            state = expect(parent)
            assert not state["dap"] and not state["ready"], state

    state = device.status()
    assert tuple(state[k] for k in ("key_up_gpio", "key_down_gpio",
                                    "key_enter_gpio", "key_back_gpio")) == (0, 1, 3, 2), state
    passed("Runtime reports Up=0, Down=1, Enter=3 and dedicated Back=2")

    # Direct cards, each tool, and About opened from both of its parents.
    pages = ((1, None, 0, TOOLS, HOME), (2, None, 0, 2, HOME),
             (3, None, 0, 3, HOME), (4, None, 0, SETTINGS, HOME),
             (0, None, 0, GAMES, HOME),
             (1, 0, 6, 5, TOOLS), (1, 1, 6, 6, TOOLS),
             (1, 2, 6, 7, TOOLS), (1, 3, 6, 8, TOOLS),
             (1, 4, 6, 9, TOOLS), (1, 5, 6, 10, TOOLS),
             (4, 4, 5, 10, SETTINGS))
    for card, row, size, view, parent in pages:
        open_page(card, row, size)
        expect(view)
        device.key("enter", "long")
        expect(view)
        for key in ("up", "down"):
            device.key(key, "press")
        expect(view)
        for key in ("up", "down"):
            device.key(key, "release")
        exit_once(view, parent)
    passed("Every ordinary page exits one level; Enter long and Up/Down no longer exit")
    passed("Leaving Wi-Fi DAP stops its service; About preserves either parent")

    for index in range(8):
        for phase in (READY, PLAYING, PAUSED):
            open_page(0, index, 8)
            view = 12 + index
            expect(view)
            if phase != READY:
                device.key("enter", "press")
                device.key("enter", "release")
            if phase == PAUSED:
                device.key("enter", "long")
            assert expect(view)["game_state"] == phase
            exit_once(view, GAMES)
        passed(f"Game {index + 1}: Back exits from ready, playing and paused without repeated returns")

    for sleeping in (False, True):
        device.open_home_card(5)
        expect(PET)
        if sleeping:
            reply = device.command("pet action sleep")
            assert "pet_ok" in reply, reply
            assert "action=sleep" in device.command("pet status")
        exit_once(PET, HOME)
    exit_once(HOME, HOME)
    passed("Awake and sleeping pets exit immediately; Back on Home stays on Home")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--output", type=Path, default=Path("build/hardware/exit"))
    args = parser.parse_args()
    result = {"ok": False, "checks": []}
    device = Device(args.port, args.output / "exit-smoke.log")
    initial = None

    def passed(name):
        result["checks"].append(name)
        print("PASS " + name, flush=True)

    try:
        initial = device.status()
        result["initial"] = initial
        assert not initial["client"], "Close the active DAP client before running this test"
        exercise(device, passed)
        result["ok"] = True
    except Exception as error:
        result["error"] = str(error)
        raise
    finally:
        try:
            if initial and not initial["client"]:
                device.home()
                for name in ("wifi", "dap"):
                    if device.status()[name] != initial[name]:
                        device.command(name + (" on" if initial[name] else " off"))
                        time.sleep(0.2)
                final = device.status()
                result["final"] = final
                for key in ("motion", "brightness", "header", "wifi", "dap", "drops", "spi_errors"):
                    assert final[key] == initial[key], (key, initial, final)
                assert final["heap"] > 50000 and final["ui_stack_free"] > 768, final
                passed("Settings, wireless state, heap, stack and input/display health preserved")
        except Exception as error:
            result["cleanup_error"] = str(error)
            result["ok"] = False
            raise
        finally:
            device.close()
            (args.output / "exit-smoke.json").write_text(
                json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    assert result["ok"], result


if __name__ == "__main__":
    main()
