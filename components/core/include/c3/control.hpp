#pragma once

#include "c3/pet.hpp"

namespace c3::control
{
struct PetChange
{
    int8_t style = -1;
    PetAction action = PetAction::count;
    bool close = false;
};
struct DisplayPatch { int8_t motion = -1, brightness = -1, header = -1; };
struct Station { char ssid[33]{}, password[64]{}; bool reconnect = false; };

bool read_code(std::string_view body, uint32_t& code);
bool read_pet(std::string_view body, PetChange& change);
bool read_display(std::string_view body, DisplayPatch& patch);
bool read_station(std::string_view body, Station& station);
}  // namespace c3::control
