#pragma once

#include <string_view>

namespace test::arcade_dips {

inline constexpr std::string_view expected = R"DIPS(generic|default|| |ffffffff
generic|reset|| |ffffffff
generic|power|| |ffffffff
11beat|default|| |ffffffff
11beat|setting|Test Mode|false|false|false|true
11beat|set|Test Mode|false|a55affff
11beat|repeat|Test Mode|false|3cc3ffff
11beat|set|Test Mode|true|a55affff
11beat|repeat|Test Mode|true|3cc3ffff
11beat|reverse|Test Mode|true|5aa5ffff
11beat|repeat|Test Mode|true|3cc3ffff
11beat|reverse|Test Mode|false|5aa5ffff
11beat|repeat|Test Mode|false|3cc3ffff
11beat|reset|| |3cc3ffff
11beat|power|| |ffffffff
11beat|retained|Test Mode|false|ffffffff
starsldr|default|| |7fcfffff
starsldr|setting|Test Mode|false|false|false|true
starsldr|set|Test Mode|false|a55affff
starsldr|repeat|Test Mode|false|3cc3ffff
starsldr|set|Test Mode|true|a55affff
starsldr|repeat|Test Mode|true|3cc3ffff
starsldr|reverse|Test Mode|true|5aa5ffff
starsldr|repeat|Test Mode|true|3cc3ffff
starsldr|reverse|Test Mode|false|5aa5ffff
starsldr|repeat|Test Mode|false|3cc3ffff
starsldr|setting|Joystick Type|2D|2D|2D|3D
starsldr|set|Joystick Type|2D|a55affff
starsldr|repeat|Joystick Type|2D|3cc3ffff
starsldr|set|Joystick Type|3D|a55affff
starsldr|repeat|Joystick Type|3D|3cc3ffff
starsldr|reverse|Joystick Type|3D|5aa5ffff
starsldr|repeat|Joystick Type|3D|3cc3ffff
starsldr|reverse|Joystick Type|2D|5aa5ffff
starsldr|repeat|Joystick Type|2D|3cc3ffff
starsldr|setting|Player|3|3|4|3|2|1
starsldr|set|Player|4|b55affff
starsldr|repeat|Player|4|3cc3ffff
starsldr|set|Player|3|bd5affff
starsldr|repeat|Player|3|3cc3ffff
starsldr|set|Player|2|ad5affff
starsldr|repeat|Player|2|3cc3ffff
starsldr|set|Player|1|a55affff
starsldr|repeat|Player|1|3cc3ffff
starsldr|reverse|Player|1|5aa5ffff
starsldr|repeat|Player|1|3cc3ffff
starsldr|reverse|Player|2|4aa5ffff
starsldr|repeat|Player|2|3cc3ffff
starsldr|reverse|Player|3|5aa5ffff
starsldr|repeat|Player|3|3cc3ffff
starsldr|reverse|Player|4|52a5ffff
starsldr|repeat|Player|4|3cc3ffff
starsldr|setting|Auto Level|Normal|Normal|Normal|Slow|Fast1|Fast2
starsldr|set|Auto Level|Normal|a55affff
starsldr|repeat|Auto Level|Normal|3cc3ffff
starsldr|set|Auto Level|Slow|c55affff
starsldr|repeat|Auto Level|Slow|3cc3ffff
starsldr|set|Auto Level|Fast1|a55affff
starsldr|repeat|Auto Level|Fast1|3cc3ffff
starsldr|set|Auto Level|Fast2|855affff
starsldr|repeat|Auto Level|Fast2|3cc3ffff
starsldr|reverse|Auto Level|Fast2|5aa5ffff
starsldr|repeat|Auto Level|Fast2|3cc3ffff
starsldr|reverse|Auto Level|Fast1|3aa5ffff
starsldr|repeat|Auto Level|Fast1|3cc3ffff
starsldr|reverse|Auto Level|Slow|5aa5ffff
starsldr|repeat|Auto Level|Slow|3cc3ffff
starsldr|reverse|Auto Level|Normal|7aa5ffff
starsldr|repeat|Auto Level|Normal|3cc3ffff
starsldr|setting|Coinage|1 Coin 1 Credit|1 Coin 1 Credit|1 Coin 1 Credit|1 Coin 2 Credits|1 Coin 3 Credits|1 Coin 4 Credits|2 Coins 1 Credit|3 Coins 1 Credit|4 Coins 1 Credit|5 Coins 1 Credit
starsldr|set|Coinage|1 Coin 1 Credit|a55affff
starsldr|repeat|Coinage|1 Coin 1 Credit|3cc3ffff
starsldr|set|Coinage|1 Coin 2 Credits|a65affff
starsldr|repeat|Coinage|1 Coin 2 Credits|3cc3ffff
starsldr|set|Coinage|1 Coin 3 Credits|a55affff
starsldr|repeat|Coinage|1 Coin 3 Credits|3cc3ffff
starsldr|set|Coinage|1 Coin 4 Credits|a45affff
starsldr|repeat|Coinage|1 Coin 4 Credits|3cc3ffff
starsldr|set|Coinage|2 Coins 1 Credit|a35affff
starsldr|repeat|Coinage|2 Coins 1 Credit|3cc3ffff
starsldr|set|Coinage|3 Coins 1 Credit|a25affff
starsldr|repeat|Coinage|3 Coins 1 Credit|3cc3ffff
starsldr|set|Coinage|4 Coins 1 Credit|a15affff
starsldr|repeat|Coinage|4 Coins 1 Credit|3cc3ffff
starsldr|set|Coinage|5 Coins 1 Credit|a05affff
starsldr|repeat|Coinage|5 Coins 1 Credit|3cc3ffff
starsldr|reverse|Coinage|5 Coins 1 Credit|5aa5ffff
starsldr|repeat|Coinage|5 Coins 1 Credit|3cc3ffff
starsldr|reverse|Coinage|4 Coins 1 Credit|59a5ffff
starsldr|repeat|Coinage|4 Coins 1 Credit|3cc3ffff
starsldr|reverse|Coinage|3 Coins 1 Credit|5aa5ffff
starsldr|repeat|Coinage|3 Coins 1 Credit|3cc3ffff
starsldr|reverse|Coinage|2 Coins 1 Credit|5ba5ffff
starsldr|repeat|Coinage|2 Coins 1 Credit|3cc3ffff
starsldr|reverse|Coinage|1 Coin 4 Credits|5ca5ffff
starsldr|repeat|Coinage|1 Coin 4 Credits|3cc3ffff
starsldr|reverse|Coinage|1 Coin 3 Credits|5da5ffff
starsldr|repeat|Coinage|1 Coin 3 Credits|3cc3ffff
starsldr|reverse|Coinage|1 Coin 2 Credits|5ea5ffff
starsldr|repeat|Coinage|1 Coin 2 Credits|3cc3ffff
starsldr|reverse|Coinage|1 Coin 1 Credit|5fa5ffff
starsldr|repeat|Coinage|1 Coin 1 Credit|3cc3ffff
starsldr|setting|Language|English|English|English|Japanese
starsldr|set|Language|English|a55affff
starsldr|repeat|Language|English|3cc3ffff
starsldr|set|Language|Japanese|a51affff
starsldr|repeat|Language|Japanese|3cc3ffff
starsldr|reverse|Language|Japanese|5aa5ffff
starsldr|repeat|Language|Japanese|3cc3ffff
starsldr|reverse|Language|English|5ae5ffff
starsldr|repeat|Language|English|3cc3ffff
starsldr|setting|Demo Sound|true|true|false|true
starsldr|set|Demo Sound|false|a57affff
starsldr|repeat|Demo Sound|false|3cc3ffff
starsldr|set|Demo Sound|true|a55affff
starsldr|repeat|Demo Sound|true|3cc3ffff
starsldr|reverse|Demo Sound|true|5aa5ffff
starsldr|repeat|Demo Sound|true|3cc3ffff
starsldr|reverse|Demo Sound|false|5aa5ffff
starsldr|repeat|Demo Sound|false|3cc3ffff
starsldr|setting|Rapid|false|false|false|true
starsldr|set|Rapid|false|a55affff
starsldr|repeat|Rapid|false|3cc3ffff
starsldr|set|Rapid|true|a55affff
starsldr|repeat|Rapid|true|3cc3ffff
starsldr|reverse|Rapid|true|5aa5ffff
starsldr|repeat|Rapid|true|3cc3ffff
starsldr|reverse|Rapid|false|5aa5ffff
starsldr|repeat|Rapid|false|3cc3ffff
starsldr|setting|Extend|30,000,000|30000000|30,000,000|50,000,000|70,000,000|None
starsldr|set|Extend|30,000,000|a55effff
starsldr|repeat|Extend|30,000,000|3cc3ffff
starsldr|set|Extend|50,000,000|a55affff
starsldr|repeat|Extend|50,000,000|3cc3ffff
starsldr|set|Extend|70,000,000|a556ffff
starsldr|repeat|Extend|70,000,000|3cc3ffff
starsldr|set|Extend|None|a552ffff
starsldr|repeat|Extend|None|3cc3ffff
starsldr|reverse|Extend|None|5aa5ffff
starsldr|repeat|Extend|None|3cc3ffff
starsldr|reverse|Extend|70,000,000|5aa5ffff
starsldr|repeat|Extend|70,000,000|3cc3ffff
starsldr|reverse|Extend|50,000,000|5aa9ffff
starsldr|repeat|Extend|50,000,000|3cc3ffff
starsldr|reverse|Extend|30,000,000|5aadffff
starsldr|repeat|Extend|30,000,000|3cc3ffff
starsldr|setting|Difficulty|Normal|Normal|Normal|Easy|Hard1|Hard2
starsldr|set|Difficulty|Normal|a55affff
starsldr|repeat|Difficulty|Normal|3cc3ffff
starsldr|set|Difficulty|Easy|a55affff
starsldr|repeat|Difficulty|Easy|3cc3ffff
starsldr|set|Difficulty|Hard1|a559ffff
starsldr|repeat|Difficulty|Hard1|3cc3ffff
starsldr|set|Difficulty|Hard2|a558ffff
starsldr|repeat|Difficulty|Hard2|3cc3ffff
starsldr|reverse|Difficulty|Hard2|5aa5ffff
starsldr|repeat|Difficulty|Hard2|3cc3ffff
starsldr|reverse|Difficulty|Hard1|5aa5ffff
starsldr|repeat|Difficulty|Hard1|3cc3ffff
starsldr|reverse|Difficulty|Easy|5aa6ffff
starsldr|repeat|Difficulty|Easy|3cc3ffff
starsldr|reverse|Difficulty|Normal|5aa7ffff
starsldr|repeat|Difficulty|Normal|3cc3ffff
starsldr|reset|| |3cc3ffff
starsldr|power|| |7fcfffff
starsldr|retained|Test Mode|false|7fcfffff
starsldr|retained|Joystick Type|2D|7fcfffff
starsldr|retained|Player|4|7fcfffff
starsldr|retained|Auto Level|Normal|7fcfffff
starsldr|retained|Coinage|1 Coin 1 Credit|7fcfffff
starsldr|retained|Language|English|7fcfffff
starsldr|retained|Demo Sound|false|7fcfffff
starsldr|retained|Rapid|false|7fcfffff
starsldr|retained|Extend|30,000,000|7fcfffff
starsldr|retained|Difficulty|Normal|7fcfffff
doncdoon|default|| |ffffffff
doncdoon|setting|Test Mode|false|false|false|true
doncdoon|set|Test Mode|false|a55affff
doncdoon|repeat|Test Mode|false|3cc3ffff
doncdoon|set|Test Mode|true|a45affff
doncdoon|repeat|Test Mode|true|3cc3ffff
doncdoon|reverse|Test Mode|true|5aa5ffff
doncdoon|repeat|Test Mode|true|3cc3ffff
doncdoon|reverse|Test Mode|false|5ba5ffff
doncdoon|repeat|Test Mode|false|3cc3ffff
doncdoon|reset|| |3cc3ffff
doncdoon|power|| |ffffffff
doncdoon|retained|Test Mode|false|ffffffff
kurufev|default|| |ffffffff
kurufev|setting|Test Mode|false|false|false|true
kurufev|set|Test Mode|false|a55affff
kurufev|repeat|Test Mode|false|3cc3ffff
kurufev|set|Test Mode|true|a45affff
kurufev|repeat|Test Mode|true|3cc3ffff
kurufev|reverse|Test Mode|true|5aa5ffff
kurufev|repeat|Test Mode|true|3cc3ffff
kurufev|reverse|Test Mode|false|5ba5ffff
kurufev|repeat|Test Mode|false|3cc3ffff
kurufev|reset|| |3cc3ffff
kurufev|power|| |ffffffff
kurufev|retained|Test Mode|false|ffffffff
mayjin3|default|| |ffffffff
mayjin3|setting|Test Mode|false|false|false|true
mayjin3|set|Test Mode|false|a55affff
mayjin3|repeat|Test Mode|false|3cc3ffff
mayjin3|set|Test Mode|true|a55affff
mayjin3|repeat|Test Mode|true|3cc3ffff
mayjin3|reverse|Test Mode|true|5aa5ffff
mayjin3|repeat|Test Mode|true|3cc3ffff
mayjin3|reverse|Test Mode|false|5aa5ffff
mayjin3|repeat|Test Mode|false|3cc3ffff
mayjin3|reset|| |3cc3ffff
mayjin3|power|| |ffffffff
mayjin3|retained|Test Mode|false|ffffffff
vivdolls|default|| |ffffffff
vivdolls|setting|Test Mode|false|false|false|true
vivdolls|set|Test Mode|false|a55affff
vivdolls|repeat|Test Mode|false|3cc3ffff
vivdolls|set|Test Mode|true|a55affff
vivdolls|repeat|Test Mode|true|3cc3ffff
vivdolls|reverse|Test Mode|true|5aa5ffff
vivdolls|repeat|Test Mode|true|3cc3ffff
vivdolls|reverse|Test Mode|false|5aa5ffff
vivdolls|repeat|Test Mode|false|3cc3ffff
vivdolls|setting|Coinage|1 Coin 1 Credit|1 Coin 1 Credit|1 Coin 1 Credit|1 Coin 2 Credits|1 Coin 3 Credits|1 Coin 4 Credits|2 Coins 1 Credit|3 Coins 1 Credit|4 Coins 1 Credit|5 Coins 1 Credit
vivdolls|set|Coinage|1 Coin 1 Credit|a55affff
vivdolls|repeat|Coinage|1 Coin 1 Credit|3cc3ffff
vivdolls|set|Coinage|1 Coin 2 Credits|a35affff
vivdolls|repeat|Coinage|1 Coin 2 Credits|3cc3ffff
vivdolls|set|Coinage|1 Coin 3 Credits|a55affff
vivdolls|repeat|Coinage|1 Coin 3 Credits|3cc3ffff
vivdolls|set|Coinage|1 Coin 4 Credits|a15affff
vivdolls|repeat|Coinage|1 Coin 4 Credits|3cc3ffff
vivdolls|set|Coinage|2 Coins 1 Credit|a65affff
vivdolls|repeat|Coinage|2 Coins 1 Credit|3cc3ffff
vivdolls|set|Coinage|3 Coins 1 Credit|a25affff
vivdolls|repeat|Coinage|3 Coins 1 Credit|3cc3ffff
vivdolls|set|Coinage|4 Coins 1 Credit|a45affff
vivdolls|repeat|Coinage|4 Coins 1 Credit|3cc3ffff
vivdolls|set|Coinage|5 Coins 1 Credit|a05affff
vivdolls|repeat|Coinage|5 Coins 1 Credit|3cc3ffff
vivdolls|reverse|Coinage|5 Coins 1 Credit|5aa5ffff
vivdolls|repeat|Coinage|5 Coins 1 Credit|3cc3ffff
vivdolls|reverse|Coinage|4 Coins 1 Credit|5ca5ffff
vivdolls|repeat|Coinage|4 Coins 1 Credit|3cc3ffff
vivdolls|reverse|Coinage|3 Coins 1 Credit|5aa5ffff
vivdolls|repeat|Coinage|3 Coins 1 Credit|3cc3ffff
vivdolls|reverse|Coinage|2 Coins 1 Credit|5ea5ffff
vivdolls|repeat|Coinage|2 Coins 1 Credit|3cc3ffff
vivdolls|reverse|Coinage|1 Coin 4 Credits|59a5ffff
vivdolls|repeat|Coinage|1 Coin 4 Credits|3cc3ffff
vivdolls|reverse|Coinage|1 Coin 3 Credits|5da5ffff
vivdolls|repeat|Coinage|1 Coin 3 Credits|3cc3ffff
vivdolls|reverse|Coinage|1 Coin 2 Credits|5ba5ffff
vivdolls|repeat|Coinage|1 Coin 2 Credits|3cc3ffff
vivdolls|reverse|Coinage|1 Coin 1 Credit|5fa5ffff
vivdolls|repeat|Coinage|1 Coin 1 Credit|3cc3ffff
vivdolls|setting|Qualify Area|70%|70%|70%|80%
vivdolls|set|Qualify Area|70%|a55affff
vivdolls|repeat|Qualify Area|70%|3cc3ffff
vivdolls|set|Qualify Area|80%|a57affff
vivdolls|repeat|Qualify Area|80%|3cc3ffff
vivdolls|reverse|Qualify Area|80%|5aa5ffff
vivdolls|repeat|Qualify Area|80%|3cc3ffff
vivdolls|reverse|Qualify Area|70%|5a85ffff
vivdolls|repeat|Qualify Area|70%|3cc3ffff
vivdolls|setting|Controls|JOYSTICK|JOYSTICK|JAMMASTICK|JOYSTICK
vivdolls|set|Controls|JAMMASTICK|a55affff
vivdolls|repeat|Controls|JAMMASTICK|3cc3ffff
vivdolls|set|Controls|JOYSTICK|a57affff
vivdolls|repeat|Controls|JOYSTICK|3cc3ffff
vivdolls|reverse|Controls|JOYSTICK|5aa5ffff
vivdolls|repeat|Controls|JOYSTICK|3cc3ffff
vivdolls|reverse|Controls|JAMMASTICK|5a85ffff
vivdolls|repeat|Controls|JAMMASTICK|3cc3ffff
vivdolls|setting|Lives|4|4|4|3|2|1
vivdolls|set|Lives|4|a55affff
vivdolls|repeat|Lives|4|3cc3ffff
vivdolls|set|Lives|3|a55affff
vivdolls|repeat|Lives|3|3cc3ffff
vivdolls|set|Lives|2|a556ffff
vivdolls|repeat|Lives|2|3cc3ffff
vivdolls|set|Lives|1|a552ffff
vivdolls|repeat|Lives|1|3cc3ffff
vivdolls|reverse|Lives|1|5aa5ffff
vivdolls|repeat|Lives|1|3cc3ffff
vivdolls|reverse|Lives|2|5aa5ffff
vivdolls|repeat|Lives|2|3cc3ffff
vivdolls|reverse|Lives|3|5aa9ffff
vivdolls|repeat|Lives|3|3cc3ffff
vivdolls|reverse|Lives|4|5aadffff
vivdolls|repeat|Lives|4|3cc3ffff
vivdolls|setting|Difficulty|Normal|Normal|Normal|Easy|Hard|Hardest
vivdolls|set|Difficulty|Normal|a55affff
vivdolls|repeat|Difficulty|Normal|3cc3ffff
vivdolls|set|Difficulty|Easy|a55affff
vivdolls|repeat|Difficulty|Easy|3cc3ffff
vivdolls|set|Difficulty|Hard|a559ffff
vivdolls|repeat|Difficulty|Hard|3cc3ffff
vivdolls|set|Difficulty|Hardest|a558ffff
vivdolls|repeat|Difficulty|Hardest|3cc3ffff
vivdolls|reverse|Difficulty|Hardest|5aa5ffff
vivdolls|repeat|Difficulty|Hardest|3cc3ffff
vivdolls|reverse|Difficulty|Hard|5aa5ffff
vivdolls|repeat|Difficulty|Hard|3cc3ffff
vivdolls|reverse|Difficulty|Easy|5aa6ffff
vivdolls|repeat|Difficulty|Easy|3cc3ffff
vivdolls|reverse|Difficulty|Normal|5aa7ffff
vivdolls|repeat|Difficulty|Normal|3cc3ffff
vivdolls|reset|| |3cc3ffff
vivdolls|power|| |ffffffff
vivdolls|retained|Test Mode|false|ffffffff
vivdolls|retained|Coinage|1 Coin 1 Credit|ffffffff
vivdolls|retained|Qualify Area|70%|ffffffff
vivdolls|retained|Controls|JAMMASTICK|ffffffff
vivdolls|retained|Lives|4|ffffffff
vivdolls|retained|Difficulty|Normal|ffffffff
twrshaft|default|| |ffffffff
twrshaft|setting|Test Mode|false|false|false|true
twrshaft|set|Test Mode|false|a55affff
twrshaft|repeat|Test Mode|false|3cc3ffff
twrshaft|set|Test Mode|true|a55affff
twrshaft|repeat|Test Mode|true|3cc3ffff
twrshaft|reverse|Test Mode|true|5aa5ffff
twrshaft|repeat|Test Mode|true|3cc3ffff
twrshaft|reverse|Test Mode|false|5aa5ffff
twrshaft|repeat|Test Mode|false|3cc3ffff
twrshaft|reset|| |3cc3ffff
twrshaft|power|| |ffffffff
twrshaft|retained|Test Mode|false|ffffffff
hipai|default|| |ffc7ffff
hipai|setting|Test Mode|false|false|false|true
hipai|set|Test Mode|false|a55affff
hipai|repeat|Test Mode|false|3cc3ffff
hipai|set|Test Mode|true|a55affff
hipai|repeat|Test Mode|true|3cc3ffff
hipai|reverse|Test Mode|true|5aa5ffff
hipai|repeat|Test Mode|true|3cc3ffff
hipai|reverse|Test Mode|false|5aa5ffff
hipai|repeat|Test Mode|false|3cc3ffff
hipai|setting|Free Play|false|false|false|true
hipai|set|Free Play|false|a55affff
hipai|repeat|Free Play|false|3cc3ffff
hipai|set|Free Play|true|255affff
hipai|repeat|Free Play|true|3cc3ffff
hipai|reverse|Free Play|true|5aa5ffff
hipai|repeat|Free Play|true|3cc3ffff
hipai|reverse|Free Play|false|daa5ffff
hipai|repeat|Free Play|false|3cc3ffff
hipai|setting|Coinage|1 Coin 1 Credit|1 Coin 1 Credit|1 Coin 1 Credit|1 Coin 2 Credits|1 Coin 3 Credits|1 Coin 4 Credits|1 Coins 2 Credit|3 Coins 1 Credit|4 Coins 1 Credit|5 Coins 1 Credit
hipai|set|Coinage|1 Coin 1 Credit|a55affff
hipai|repeat|Coinage|1 Coin 1 Credit|3cc3ffff
hipai|set|Coinage|1 Coin 2 Credits|a35affff
hipai|repeat|Coinage|1 Coin 2 Credits|3cc3ffff
hipai|set|Coinage|1 Coin 3 Credits|a55affff
hipai|repeat|Coinage|1 Coin 3 Credits|3cc3ffff
hipai|set|Coinage|1 Coin 4 Credits|a45affff
hipai|repeat|Coinage|1 Coin 4 Credits|3cc3ffff
hipai|set|Coinage|1 Coins 2 Credit|a55affff
hipai|repeat|Coinage|1 Coins 2 Credit|3cc3ffff
hipai|set|Coinage|3 Coins 1 Credit|a25affff
hipai|repeat|Coinage|3 Coins 1 Credit|3cc3ffff
hipai|set|Coinage|4 Coins 1 Credit|a15affff
hipai|repeat|Coinage|4 Coins 1 Credit|3cc3ffff
hipai|set|Coinage|5 Coins 1 Credit|a05affff
hipai|repeat|Coinage|5 Coins 1 Credit|3cc3ffff
hipai|reverse|Coinage|5 Coins 1 Credit|5aa5ffff
hipai|repeat|Coinage|5 Coins 1 Credit|3cc3ffff
hipai|reverse|Coinage|4 Coins 1 Credit|59a5ffff
hipai|repeat|Coinage|4 Coins 1 Credit|3cc3ffff
hipai|reverse|Coinage|3 Coins 1 Credit|5aa5ffff
hipai|repeat|Coinage|3 Coins 1 Credit|3cc3ffff
hipai|reverse|Coinage|1 Coins 2 Credit|5aa5ffff
hipai|repeat|Coinage|1 Coins 2 Credit|3cc3ffff
hipai|reverse|Coinage|1 Coin 4 Credits|5ca5ffff
hipai|repeat|Coinage|1 Coin 4 Credits|3cc3ffff
hipai|reverse|Coinage|1 Coin 3 Credits|5da5ffff
hipai|repeat|Coinage|1 Coin 3 Credits|3cc3ffff
hipai|reverse|Coinage|1 Coin 2 Credits|5ba5ffff
hipai|repeat|Coinage|1 Coin 2 Credits|3cc3ffff
hipai|reverse|Coinage|1 Coin 1 Credit|5fa5ffff
hipai|repeat|Coinage|1 Coin 1 Credit|3cc3ffff
hipai|setting|Demo Sounds|true|true|false|true
hipai|set|Demo Sounds|false|a57affff
hipai|repeat|Demo Sounds|false|3cc3ffff
hipai|set|Demo Sounds|true|a55affff
hipai|repeat|Demo Sounds|true|3cc3ffff
hipai|reverse|Demo Sounds|true|5aa5ffff
hipai|repeat|Demo Sounds|true|3cc3ffff
hipai|reverse|Demo Sounds|false|5aa5ffff
hipai|repeat|Demo Sounds|false|3cc3ffff
hipai|setting|Allow Continue|false|false|false|true
hipai|set|Allow Continue|false|a55affff
hipai|repeat|Allow Continue|false|3cc3ffff
hipai|set|Allow Continue|true|a55affff
hipai|repeat|Allow Continue|true|3cc3ffff
hipai|reverse|Allow Continue|true|5aa5ffff
hipai|repeat|Allow Continue|true|3cc3ffff
hipai|reverse|Allow Continue|false|5aa5ffff
hipai|repeat|Allow Continue|false|3cc3ffff
hipai|setting|Kuitan|false|false|false|true
hipai|set|Kuitan|false|a55affff
hipai|repeat|Kuitan|false|3cc3ffff
hipai|set|Kuitan|true|a55affff
hipai|repeat|Kuitan|true|3cc3ffff
hipai|reverse|Kuitan|true|5aa5ffff
hipai|repeat|Kuitan|true|3cc3ffff
hipai|reverse|Kuitan|false|5aa5ffff
hipai|repeat|Kuitan|false|3cc3ffff
hipai|setting|Difficulty|Normal|Normal|Normal|Easiest|Very Easy|Easy|Normal+|Hard|Very Hard|Most Hard
hipai|set|Difficulty|Normal|a55affff
hipai|repeat|Difficulty|Normal|3cc3ffff
hipai|set|Difficulty|Easiest|a55effff
hipai|repeat|Difficulty|Easiest|3cc3ffff
hipai|set|Difficulty|Very Easy|a55dffff
hipai|repeat|Difficulty|Very Easy|3cc3ffff
hipai|set|Difficulty|Easy|a55cffff
hipai|repeat|Difficulty|Easy|3cc3ffff
hipai|set|Difficulty|Normal+|a55bffff
hipai|repeat|Difficulty|Normal+|3cc3ffff
hipai|set|Difficulty|Hard|a55affff
hipai|repeat|Difficulty|Hard|3cc3ffff
hipai|set|Difficulty|Very Hard|a559ffff
hipai|repeat|Difficulty|Very Hard|3cc3ffff
hipai|set|Difficulty|Most Hard|a558ffff
hipai|repeat|Difficulty|Most Hard|3cc3ffff
hipai|reverse|Difficulty|Most Hard|5aa5ffff
hipai|repeat|Difficulty|Most Hard|3cc3ffff
hipai|reverse|Difficulty|Very Hard|5aa1ffff
hipai|repeat|Difficulty|Very Hard|3cc3ffff
hipai|reverse|Difficulty|Hard|5aa2ffff
hipai|repeat|Difficulty|Hard|3cc3ffff
hipai|reverse|Difficulty|Normal+|5aa3ffff
hipai|repeat|Difficulty|Normal+|3cc3ffff
hipai|reverse|Difficulty|Easy|5aa4ffff
hipai|repeat|Difficulty|Easy|3cc3ffff
hipai|reverse|Difficulty|Very Easy|5aa5ffff
hipai|repeat|Difficulty|Very Easy|3cc3ffff
hipai|reverse|Difficulty|Easiest|5aa6ffff
hipai|repeat|Difficulty|Easiest|3cc3ffff
hipai|reverse|Difficulty|Normal|5aa7ffff
hipai|repeat|Difficulty|Normal|3cc3ffff
hipai|reset|| |3cc3ffff
hipai|power|| |ffc7ffff
hipai|retained|Test Mode|false|ffc7ffff
hipai|retained|Free Play|false|ffc7ffff
hipai|retained|Coinage|1 Coin 1 Credit|ffc7ffff
hipai|retained|Demo Sounds|false|ffc7ffff
hipai|retained|Allow Continue|false|ffc7ffff
hipai|retained|Kuitan|false|ffc7ffff
hipai|retained|Difficulty|Normal|ffc7ffff
hipai2|default|| |ffc7ffff
hipai2|setting|Test Mode|false|false|false|true
hipai2|set|Test Mode|false|a55affff
hipai2|repeat|Test Mode|false|3cc3ffff
hipai2|set|Test Mode|true|a55affff
hipai2|repeat|Test Mode|true|3cc3ffff
hipai2|reverse|Test Mode|true|5aa5ffff
hipai2|repeat|Test Mode|true|3cc3ffff
hipai2|reverse|Test Mode|false|5aa5ffff
hipai2|repeat|Test Mode|false|3cc3ffff
hipai2|setting|Free Play|false|false|false|true
hipai2|set|Free Play|false|a55affff
hipai2|repeat|Free Play|false|3cc3ffff
hipai2|set|Free Play|true|255affff
hipai2|repeat|Free Play|true|3cc3ffff
hipai2|reverse|Free Play|true|5aa5ffff
hipai2|repeat|Free Play|true|3cc3ffff
hipai2|reverse|Free Play|false|daa5ffff
hipai2|repeat|Free Play|false|3cc3ffff
hipai2|setting|Backup Settings|false|false|false|true
hipai2|set|Backup Settings|false|a55affff
hipai2|repeat|Backup Settings|false|3cc3ffff
hipai2|set|Backup Settings|true|a55affff
hipai2|repeat|Backup Settings|true|3cc3ffff
hipai2|reverse|Backup Settings|true|5aa5ffff
hipai2|repeat|Backup Settings|true|3cc3ffff
hipai2|reverse|Backup Settings|false|5aa5ffff
hipai2|repeat|Backup Settings|false|3cc3ffff
hipai2|setting|Coinage|1 Coin 1 Credit|1 Coin 1 Credit|1 Coin 1 Credit|1 Coin 2 Credits|1 Coin 3 Credits|1 Coin 4 Credits|1 Coins 2 Credit|3 Coins 1 Credit|4 Coins 1 Credit|5 Coins 1 Credit
hipai2|set|Coinage|1 Coin 1 Credit|a55affff
hipai2|repeat|Coinage|1 Coin 1 Credit|3cc3ffff
hipai2|set|Coinage|1 Coin 2 Credits|a35affff
hipai2|repeat|Coinage|1 Coin 2 Credits|3cc3ffff
hipai2|set|Coinage|1 Coin 3 Credits|a55affff
hipai2|repeat|Coinage|1 Coin 3 Credits|3cc3ffff
hipai2|set|Coinage|1 Coin 4 Credits|a45affff
hipai2|repeat|Coinage|1 Coin 4 Credits|3cc3ffff
hipai2|set|Coinage|1 Coins 2 Credit|a55affff
hipai2|repeat|Coinage|1 Coins 2 Credit|3cc3ffff
hipai2|set|Coinage|3 Coins 1 Credit|a25affff
hipai2|repeat|Coinage|3 Coins 1 Credit|3cc3ffff
hipai2|set|Coinage|4 Coins 1 Credit|a15affff
hipai2|repeat|Coinage|4 Coins 1 Credit|3cc3ffff
hipai2|set|Coinage|5 Coins 1 Credit|a05affff
hipai2|repeat|Coinage|5 Coins 1 Credit|3cc3ffff
hipai2|reverse|Coinage|5 Coins 1 Credit|5aa5ffff
hipai2|repeat|Coinage|5 Coins 1 Credit|3cc3ffff
hipai2|reverse|Coinage|4 Coins 1 Credit|59a5ffff
hipai2|repeat|Coinage|4 Coins 1 Credit|3cc3ffff
hipai2|reverse|Coinage|3 Coins 1 Credit|5aa5ffff
hipai2|repeat|Coinage|3 Coins 1 Credit|3cc3ffff
hipai2|reverse|Coinage|1 Coins 2 Credit|5aa5ffff
hipai2|repeat|Coinage|1 Coins 2 Credit|3cc3ffff
hipai2|reverse|Coinage|1 Coin 4 Credits|5ca5ffff
hipai2|repeat|Coinage|1 Coin 4 Credits|3cc3ffff
hipai2|reverse|Coinage|1 Coin 3 Credits|5da5ffff
hipai2|repeat|Coinage|1 Coin 3 Credits|3cc3ffff
hipai2|reverse|Coinage|1 Coin 2 Credits|5ba5ffff
hipai2|repeat|Coinage|1 Coin 2 Credits|3cc3ffff
hipai2|reverse|Coinage|1 Coin 1 Credit|5fa5ffff
hipai2|repeat|Coinage|1 Coin 1 Credit|3cc3ffff
hipai2|setting|Demo Sounds|true|true|false|true
hipai2|set|Demo Sounds|false|a57affff
hipai2|repeat|Demo Sounds|false|3cc3ffff
hipai2|set|Demo Sounds|true|a55affff
hipai2|repeat|Demo Sounds|true|3cc3ffff
hipai2|reverse|Demo Sounds|true|5aa5ffff
hipai2|repeat|Demo Sounds|true|3cc3ffff
hipai2|reverse|Demo Sounds|false|5aa5ffff
hipai2|repeat|Demo Sounds|false|3cc3ffff
hipai2|setting|Allow Continue|false|false|false|true
hipai2|set|Allow Continue|false|a55affff
hipai2|repeat|Allow Continue|false|3cc3ffff
hipai2|set|Allow Continue|true|a55affff
hipai2|repeat|Allow Continue|true|3cc3ffff
hipai2|reverse|Allow Continue|true|5aa5ffff
hipai2|repeat|Allow Continue|true|3cc3ffff
hipai2|reverse|Allow Continue|false|5aa5ffff
hipai2|repeat|Allow Continue|false|3cc3ffff
hipai2|setting|Kuitan|false|false|false|true
hipai2|set|Kuitan|false|a55affff
hipai2|repeat|Kuitan|false|3cc3ffff
hipai2|set|Kuitan|true|a55affff
hipai2|repeat|Kuitan|true|3cc3ffff
hipai2|reverse|Kuitan|true|5aa5ffff
hipai2|repeat|Kuitan|true|3cc3ffff
hipai2|reverse|Kuitan|false|5aa5ffff
hipai2|repeat|Kuitan|false|3cc3ffff
hipai2|setting|Difficulty|Normal|Normal|Normal|Easiest|Very Easy|Easy|Normal+|Hard|Very Hard|Most Hard
hipai2|set|Difficulty|Normal|a55affff
hipai2|repeat|Difficulty|Normal|3cc3ffff
hipai2|set|Difficulty|Easiest|a55effff
hipai2|repeat|Difficulty|Easiest|3cc3ffff
hipai2|set|Difficulty|Very Easy|a55dffff
hipai2|repeat|Difficulty|Very Easy|3cc3ffff
hipai2|set|Difficulty|Easy|a55cffff
hipai2|repeat|Difficulty|Easy|3cc3ffff
hipai2|set|Difficulty|Normal+|a55bffff
hipai2|repeat|Difficulty|Normal+|3cc3ffff
hipai2|set|Difficulty|Hard|a55affff
hipai2|repeat|Difficulty|Hard|3cc3ffff
hipai2|set|Difficulty|Very Hard|a559ffff
hipai2|repeat|Difficulty|Very Hard|3cc3ffff
hipai2|set|Difficulty|Most Hard|a558ffff
hipai2|repeat|Difficulty|Most Hard|3cc3ffff
hipai2|reverse|Difficulty|Most Hard|5aa5ffff
hipai2|repeat|Difficulty|Most Hard|3cc3ffff
hipai2|reverse|Difficulty|Very Hard|5aa1ffff
hipai2|repeat|Difficulty|Very Hard|3cc3ffff
hipai2|reverse|Difficulty|Hard|5aa2ffff
hipai2|repeat|Difficulty|Hard|3cc3ffff
hipai2|reverse|Difficulty|Normal+|5aa3ffff
hipai2|repeat|Difficulty|Normal+|3cc3ffff
hipai2|reverse|Difficulty|Easy|5aa4ffff
hipai2|repeat|Difficulty|Easy|3cc3ffff
hipai2|reverse|Difficulty|Very Easy|5aa5ffff
hipai2|repeat|Difficulty|Very Easy|3cc3ffff
hipai2|reverse|Difficulty|Easiest|5aa6ffff
hipai2|repeat|Difficulty|Easiest|3cc3ffff
hipai2|reverse|Difficulty|Normal|5aa7ffff
hipai2|repeat|Difficulty|Normal|3cc3ffff
hipai2|reset|| |3cc3ffff
hipai2|power|| |ffc7ffff
hipai2|retained|Test Mode|false|ffc7ffff
hipai2|retained|Free Play|false|ffc7ffff
hipai2|retained|Backup Settings|false|ffc7ffff
hipai2|retained|Coinage|1 Coin 1 Credit|ffc7ffff
hipai2|retained|Demo Sounds|false|ffc7ffff
hipai2|retained|Allow Continue|false|ffc7ffff
hipai2|retained|Kuitan|false|ffc7ffff
hipai2|retained|Difficulty|Normal|ffc7ffff
srmvs|default|| |ffffffff
srmvs|setting|Test Mode|false|false|false|true
srmvs|set|Test Mode|false|a55affff
srmvs|repeat|Test Mode|false|3cc3ffff
srmvs|set|Test Mode|true|a55affff
srmvs|repeat|Test Mode|true|3cc3ffff
srmvs|reverse|Test Mode|true|5aa5ffff
srmvs|repeat|Test Mode|true|3cc3ffff
srmvs|reverse|Test Mode|false|5aa5ffff
srmvs|repeat|Test Mode|false|3cc3ffff
srmvs|reset|| |3cc3ffff
srmvs|power|| |ffffffff
srmvs|retained|Test Mode|false|ffffffff
srmvsa|default|| |ffffffff
srmvsa|setting|Test Mode|false|false|false|true
srmvsa|set|Test Mode|false|a55affff
srmvsa|repeat|Test Mode|false|3cc3ffff
srmvsa|set|Test Mode|true|a55affff
srmvsa|repeat|Test Mode|true|3cc3ffff
srmvsa|reverse|Test Mode|true|5aa5ffff
srmvsa|repeat|Test Mode|true|3cc3ffff
srmvsa|reverse|Test Mode|false|5aa5ffff
srmvsa|repeat|Test Mode|false|3cc3ffff
srmvsa|reset|| |3cc3ffff
srmvsa|power|| |ffffffff
srmvsa|retained|Test Mode|false|ffffffff
mtetrisc|default|| |ffffffff
mtetrisc|setting|Test Mode|false|false|false|true
mtetrisc|set|Test Mode|false|a55affff
mtetrisc|repeat|Test Mode|false|3cc3ffff
mtetrisc|set|Test Mode|true|a55affff
mtetrisc|repeat|Test Mode|true|3cc3ffff
mtetrisc|reverse|Test Mode|true|5aa5ffff
mtetrisc|repeat|Test Mode|true|3cc3ffff
mtetrisc|reverse|Test Mode|false|5aa5ffff
mtetrisc|repeat|Test Mode|false|3cc3ffff
mtetrisc|reset|| |3cc3ffff
mtetrisc|power|| |ffffffff
mtetrisc|retained|Test Mode|false|ffffffff
)DIPS";

} // namespace test::arcade_dips
