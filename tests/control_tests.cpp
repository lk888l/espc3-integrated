#include <cassert>
#include <string>

#include "c3/control.hpp"
#include "c3/core.hpp"

int main()
{
    using namespace c3;
    using namespace c3::control;
    uint32_t code = 0;
    assert(read_code(R"({"code":"123456"})", code) && code == 123456);
    for (auto bad : {R"({"code":123456})", R"({"code":"1234x6"})", R"({"code":"1234567"})",
                     R"({"code":"123456","code":"654321"})"})
        assert(!read_code(bad, code));
    PetChange change;
    assert(read_pet(R"({"action":"dance","style":1})", change));
    assert(change.style == 1 && change.action == PetAction::dance);
    assert(read_pet(R"({"close":true})", change) && change.close && change.style == -1);
    for (auto bad : {"{}", R"({"style":5})", R"({"style":1,"style":0})", R"({"action":"Dance"})",
                     R"({"style":1e0})", R"({"style":0,"close":true})", R"({"close":false})", R"({"action":"eat"}[])"})
        assert(!read_pet(bad, change) && change.close);
    Station station;
    assert(read_station(R"({"password":"a\"bcdefg","ssid":"\u7f51\u7edc"})", station));
    assert(std::string(station.ssid) == "网络" && std::string(station.password) == "a\"bcdefg");
    assert(read_station("{\"ssid\":\"" + std::string(32, 's') + "\",\"password\":\"" +
                        std::string(63, 'p') + "\"}", station));
    assert(read_station(R"({"ssid":"guest","password":""})", station));
    Station existing;
    assert(read_station(R"({"reconnect":true})", existing) && existing.reconnect);
    assert(!read_station(R"({"reconnect":true,"ssid":"guest"})", existing));
    for (auto bad : {R"({"ssid":"test"})", R"({"ssid":"x\u0000y","password":""})",
                     R"({"ssid":"","password":"12345678"})", R"({"ssid":"a","password":"short"})"})
        assert(!read_station(bad, station) && std::string(station.ssid) == "guest");
    DisplayPatch patch;
    assert(read_display(R"({"brightness":0,"header":3})", patch));
    assert(patch.brightness == 0 && patch.header == 3 && patch.motion == -1);
    for (auto bad : {R"({"brightness":4})", R"({"motion":-1})", R"({"header":true})", R"({"bright":2})"})
        assert(!read_display(bad, patch));

    Model model;
    Settings next = model.settings();
    model.set_settings(next);
    assert(model.take_effects() == none);
    next.brightness = 0;
    model.set_settings(next);
    assert(model.settings().brightness == 0 && model.take_effects() == save_settings);
    model.tick(0);
    model.tick(700);
    model.open(View::network);
    model.handle({Key::up, Gesture::pressed, 710});
    assert(model.take_effects() == pair_client);
    model.handle({Key::up, Gesture::repeat, 900});
    assert(model.take_effects() == none);
    model.handle({Key::down, Gesture::pressed, 910});
    assert(model.take_effects() == revoke_clients);
    model.handle({Key::enter, Gesture::pressed, 920});
    assert(model.take_effects() == sync_time);
}
