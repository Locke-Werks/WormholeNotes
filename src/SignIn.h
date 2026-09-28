#pragma once

// Starting WormholeNotes when this user signs in.
//
// The installer can add a Run entry for everyone on the machine, which only
// an administrator can take away. So both that entry and the one this user
// can add carry --sign-in, and a copy started with it quits at once when
// this user has turned starting at sign-in off.
namespace SignIn {

inline constexpr char kArgument[] = "--sign-in";

bool enabled();
void setEnabled(bool on);
// True when this launch came from a Run entry the user has turned off.
bool shouldQuit(int argc, char *argv[]);
// Removes this user's Run entry when it names this executable; for the
// uninstaller.
void forget();

} // namespace SignIn
