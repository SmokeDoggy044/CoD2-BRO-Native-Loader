#pragma once

// Cheats (1.0.10, issues #22 and #39): god mode, infinite ammo and a free camera, on the Cheats page (pause menu >
// Settings) and in settings.ini. Campaign only, where the functions they work in run as our C: ClientEvents (god
// mode), PM_WeaponUseAmmo (infinite ammo), CG_CalcViewValues, CG_Draw2D and CG_AddPlayerWeapon (the free camera).
// All three are off by default and switch while playing.
//
// The free camera: with its cheat on, the Free camera key (input::Action::FreeCamera, B) detaches the camera from
// the player where it is; the movement keys fly it along the view, Jump rises, Crouch sinks, Sprint is faster, Aim
// slower; the mouse turns it. The player gets no input meanwhile (no keys, no mouse look, no pad), the view weapon,
// crosshair and HUD are hidden. The key again, the cheat switched off or a new level brings the camera back.

#include <cstdint>

namespace loader {

struct TitlePatchConfig;

// From install_title_patches: whether this title can have the cheats (`supported`), and their settings.
void install_cheats(bool supported, const TitlePatchConfig& config);

// The settings, switched while playing (the settings pages, the PC menu); nothing where unsupported.
void set_god_mode(bool on);
void set_infinite_ammo(bool on);
void set_free_camera(bool on);  // the key works; off also brings the camera back
bool god_mode();
bool infinite_ammo();
bool free_camera();

// native_actions: the held actions (bit = input::Action) once per input frame. Toggles the free camera on the
// key's press; returns what the game gets (nothing while the camera flies).
uint32_t cheats_filter_actions(uint32_t held);

// mouse_look: true when the free camera took the mouse motion (the player's view does not turn then).
bool cheats_take_mouse(float dx, float dy, float degrees_per_count);

// Whether the free camera flies (the pad gives the player nothing then).
bool free_camera_flying();

}  // namespace loader
