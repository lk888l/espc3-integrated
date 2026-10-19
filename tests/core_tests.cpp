#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

#include "c3/core.hpp"
#include "c3/pin_policy.hpp"
using namespace c3;
namespace
{
void button_tests()
{
    ButtonBank bank;
    std::array<KeyEvent, 32> out{};
    std::vector<KeyEvent> events;
    const auto sample = [&](uint8_t raw, uint32_t now)
    {
        const auto n = bank.sample(raw, now, out);
        events.insert(events.end(), out.begin(), out.begin() + n);
    };
    const auto count = [&](Gesture g)
    { return std::count_if(events.begin(), events.end(), [&](auto e) { return e.gesture == g; }); };
    sample(mask(Key::enter), 0);
    sample(0, 5);
    sample(mask(Key::enter), 10);
    sample(mask(Key::enter), 29);
    assert(events.empty());
    sample(mask(Key::enter), 30);
    assert(count(Gesture::pressed) == 1 && events.back().at_ms == 30);
    sample(0, 50);
    sample(0, 70);
    assert(count(Gesture::released) == 1);
    sample(0, 349);
    assert(count(Gesture::click) == 0);
    sample(0, 350);
    assert(count(Gesture::click) == 1);
    events.clear();
    sample(mask(Key::enter), 400);
    sample(mask(Key::enter), 420);
    sample(0, 450);
    sample(0, 470);
    sample(mask(Key::enter), 500);
    sample(mask(Key::enter), 520);
    sample(0, 550);
    sample(0, 570);
    assert(count(Gesture::pressed) == 2 && count(Gesture::double_click) == 1 &&
           count(Gesture::click) == 0);
    events.clear();
    sample(mask(Key::up) | mask(Key::down), 600);
    sample(mask(Key::up) | mask(Key::down), 620);
    assert(bank.stable_mask() == (mask(Key::up) | mask(Key::down)));
    sample(mask(Key::up) | mask(Key::down), 1269);
    assert(count(Gesture::long_press) == 0);
    sample(mask(Key::up) | mask(Key::down), 1270);
    assert(count(Gesture::long_press) == 2);
    sample(mask(Key::up) | mask(Key::down), 1380);
    assert(count(Gesture::repeat) == 2);
    sample(mask(Key::up) | mask(Key::down), 2000);
    assert(count(Gesture::repeat) == 4);
    sample(0, 2010);
    sample(0, 2030);
    sample(0, 3000);
    assert(count(Gesture::click) == 0);
    ButtonBank wrapping;
    out = {};
    constexpr uint32_t start = 0xfffffff0;
    assert(wrapping.sample(mask(Key::up), start, out) == 0);
    assert(wrapping.sample(mask(Key::up), uint32_t(start + 20), out) == 1 &&
           out[0].gesture == Gesture::pressed);
    ButtonBank directions;
    const std::array<KeyEvent, 2> pair_events = {KeyEvent{Key::up, Gesture::pressed, 100, 0},
                                                 KeyEvent{Key::down, Gesture::pressed, 100, 0}};
    assert(directions.sample(mask(Key::up) | mask(Key::down), 80, out) == 0);
    assert(directions.sample(mask(Key::up) | mask(Key::down), 100, out) == 2);
    for (unsigned i = 0; i < pair_events.size(); ++i)
        assert(out[i].key == pair_events[i].key && out[i].gesture == Gesture::pressed);
    ButtonConfig invalid;
    invalid.debounce_ms = 0;
    assert(!invalid.valid());
    ButtonBank bad(invalid);
    assert(bad.sample(255, 0, out) == 0);
}
void spring_tests()
{
    for (const auto motion : {Motion::full, Motion::reduced})
    {
        Spring spring(-128);
        spring.target(128);
        for (unsigned i = 0; i < 200; ++i) spring.step(motion);
        assert(spring.value() == 128 && spring.settled());
        spring.target(-100);
        spring.step(motion);
        spring.target(60);
        for (unsigned i = 0; i < 200; ++i) spring.step(motion);
        assert(spring.value() == 60 && spring.settled());
    }
    Spring disabled(-128);
    disabled.target(27);
    disabled.step(Motion::off);
    assert(disabled.value() == 27 && disabled.settled());
    Spring signature(0);
    signature.target(100);
    signature.step(Motion::full);
    assert(signature.value() == 17);  // same 62/184 Q24.8 integrator as GameBox
}
void model_tests()
{
    Model model;
    model.tick(0);
    model.tick(600);
    assert(!model.booting());
    model.handle({Key::down, Gesture::pressed, 600, 0});  // Tools follows the new Games card.
    model.handle({Key::enter, Gesture::pressed, 601, 0});
    assert(model.view() == View::tools && !model.motion().page_x.settled());
    model.handle({Key::down, Gesture::pressed, 606, 0});
    assert(model.selected(View::tools) == 1);
    model.handle({Key::enter, Gesture::pressed, 611, 0});
    assert(model.view() == View::countdown);  // a fresh press during animation must work
    model.handle({Key::enter, Gesture::released, 631, 20});
    model.handle({Key::enter, Gesture::click, 911, 20});
    assert(!model.countdown_running());  // delayed confirmation gestures never leak
    model.handle({Key::enter, Gesture::pressed, 920, 0});
    assert(model.countdown_running());
    model.tick(1920);
    assert(model.countdown_seconds() == 299);
    model.handle({Key::back, Gesture::pressed, 1921, 0});
    assert(model.view() == View::tools);
    model.open(View::debug);
    assert(model.take_effects() & start_probe);
    model.handle({Key::down, Gesture::pressed, 2000, 0});
    assert(model.probe_clock() == 250000 && (model.take_effects() & set_probe_clock));
    model.handle({Key::enter, Gesture::long_press, 2650, 650});
    assert(model.view() == View::debug && model.probe_wanted());
    assert(!(model.take_effects() & stop_probe));
    model.handle({Key::back, Gesture::pressed, 2651, 0});
    assert(model.view() == View::tools && (model.take_effects() & stop_probe));
    model.open(View::stopwatch);
    model.handle({Key::enter, Gesture::pressed, 0xfffffff0, 0});
    model.tick(uint32_t(0xfffffff0U + 100));
    assert(model.stopwatch_ms() == 100);
    model.handle({Key::enter, Gesture::pressed, uint32_t(0xfffffff0U + 110), 0});
    model.tick(500);
    assert(model.stopwatch_ms() == 110);
    model.open(View::settings);
    model.handle({Key::enter, Gesture::pressed, 501, 0});
    assert(model.settings().motion == Motion::off && (model.take_effects() & save_settings));
    Settings bad;
    bad.brightness = 255;
    Model safe(bad);
    assert(safe.settings().valid());
}
void exit_button_tests()
{
    // Run the real debounce/gesture recognizer through every page. A sustained
    // Back press must return exactly one level even after long/repeat/release.
    constexpr std::array parents = {
        View::home, View::home, View::home, View::home, View::home,
        View::tools, View::tools, View::tools, View::tools, View::tools,
        View::settings, View::home, View::games, View::games, View::games,
        View::games, View::games, View::games, View::games, View::games, View::home};
    static_assert(parents.size() == unsigned(View::count));
    for (unsigned v = 0; v < parents.size(); ++v)
    {
        Model model;
        model.tick(0);
        model.tick(600);
        if (View(v) == View::about) model.open(View::settings);
        model.open(View(v));
        model.take_effects();
        ButtonBank bank;
        std::array<KeyEvent, 32> events{};
        const auto sample = [&](uint8_t raw, uint32_t now)
        {
            const auto count = bank.sample(raw, now, events);
            for (unsigned i = 0; i < count; ++i) model.handle(events[i]);
        };
        sample(mask(Key::back), 700);
        sample(mask(Key::back), 719);
        assert(model.view() == View(v));
        sample(mask(Key::back), 720);
        assert(model.view() == parents[v]);
        if (View(v) == View::debug)
            assert(!model.probe_wanted() && (model.take_effects() & stop_probe));
        for (uint32_t now = 725; now <= 1800; now += 5)
        {
            sample(mask(Key::back), now);
            assert(model.view() == parents[v]);
        }
        sample(0, 1805);
        sample(0, 1825);
        sample(0, 2200);
        assert(model.view() == parents[v]);
        // A second physical tap is a new request, and its delayed Click must
        // not skip a level either.
        sample(mask(Key::back), 2300);
        sample(mask(Key::back), 2320);
        const auto second = model.view();
        assert(second == View::home);
        sample(0, 2340);
        sample(0, 2360);
        sample(0, 2640);
        assert(model.view() == second);
    }

    for (const auto view : {View::tools, View::network, View::clock, View::settings,
                            View::stopwatch, View::countdown, View::input_lab,
                            View::system, View::debug, View::about, View::games})
    {
        Model model;
        model.tick(0);
        model.tick(600);
        model.open(view);
        model.handle({Key::enter, Gesture::long_press, 1300, 650});
        assert(model.view() == view);
        ButtonBank bank;
        std::array<KeyEvent, 32> events{};
        bank.sample(mask(Key::up) | mask(Key::down), 1400, events);
        const auto count = bank.sample(mask(Key::up) | mask(Key::down), 1420, events);
        for (unsigned i = 0; i < count; ++i) model.handle(events[i]);
        assert(model.view() == view);
    }

    Model about;
    about.tick(0);
    about.tick(600);
    about.open(View::tools);
    about.open(View::about);
    about.handle({Key::back, Gesture::pressed, 700, 0});
    assert(about.view() == View::tools);
}
void render_tests()
{
    Frame frame{};
    Canvas canvas(frame);
    canvas.clip(0, 0, 128, 64);
    canvas.rect(-10, -10, 30, 30, PixelOp::set, true);
    assert(frame[0] == 255 && frame[19] == 255 && frame[20] == 0);
    canvas.clear();
    canvas.pixel(127, 63);
    assert(frame[1023] == 0x80);
    canvas.pixel(128, 64);
    assert(frame[1023] == 0x80);
    Model model;
    Telemetry telemetry;
    model.tick(0);
    model.tick(600);
    render(frame, model, telemetry, 600);
    const auto home = frame;
    render(frame, model, telemetry, 600);
    assert(frame == home);
    model.handle({Key::down, Gesture::pressed, 601, 0});
    model.tick(609);
    render(frame, model, telemetry, 609);
    assert(frame != home);
    for (uint32_t now = 610; now < 1800; now += 5) model.tick(now);
    render(frame, model, telemetry, 1800);
    assert(model.motion().carousel_x.settled() && frame != home);
    for (unsigned v = 0; v < unsigned(View::count); ++v)
    {
        model.open(View(v));
        for (uint32_t n = 2000; n < 3300; n += 5) model.tick(n);
        render(frame, model, telemetry, 3300);
        assert(std::any_of(frame.begin(), frame.end(), [](auto b) { return b != 0; }));
    }
}
}  // namespace
int main()
{
    button_tests();
    spring_tests();
    model_tests();
    exit_button_tests();
    render_tests();
    static_assert(board_gpio(4) && board_gpio(6) && board_gpio(7) && board_gpio(20));
    static_assert(!board_gpio(18) && !board_gpio(19) && !board_gpio(12));
    static_assert(!board_gpio(2) && !board_gpio(8) && !board_gpio(9));
    constexpr std::array<int, 4> good = {4, 6, -1, -1}, bad = {4, 6, 4, -1};
    static_assert(unique_pins(good) && !unique_pins(bad));
}
