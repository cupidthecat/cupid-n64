#pragma once

#include <string_view>

namespace cupid::n64::cartridge {

enum class SaveChip {
  None,
  Eeprom512,
  Eeprom2048,
  Sram32768,
  Sram98304,
  FlashMx29l1100,
  FlashGeneric
};
struct Board {
  std::string_view ids;
  SaveChip save;
  unsigned accessories;
};

inline constexpr Board boards[] = {
    {"NIM", SaveChip::Eeprom2048, 0},
    {"NPP", SaveChip::Eeprom2048, 1},
    {"N3D NB7 NCW NCZ ND2 ND6 NDO NEP NEV NFU NMV NRZ NYS", SaveChip::Eeprom2048, 2},
    {"NGC NGT NMX NNB", SaveChip::Eeprom2048, 3},
    {"NR7", SaveChip::Eeprom2048, 4},
    {"NUB", SaveChip::Eeprom2048, 5},
    {"NM8", SaveChip::Eeprom2048, 6},
    {"NPD", SaveChip::Eeprom2048, 7},
    {"NAD NCX NDR NEA NGV NHF NHP NJM NMO NMZ NN6 NPW NSC NSW NT6 NTM NTN NTP NWU",
     SaveChip::Eeprom512, 0},
    {"NAG NBC NBM NBN NCR NCU NDQ NHA NKI NKT NMS NS3 NTW", SaveChip::Eeprom512, 1},
    {"CLB NBD NBH NBK NCH NCT NDU NER NF2 NFH NFW NFX NGE NGU NIC NIJ NIR NJK NK2 NLB NLL NLR NMI "
     "NMW NNA NPG NPY NRC NRS NS6 NSA NSS NSU NSV NTB NTC NTJ NTX NVL NVY NWC NXO NYK",
     SaveChip::Eeprom512, 2},
    {"NAB NBV NDY NFG NFY NGL NKA NMG NMR NMU NOS NP2 NRA NSN NTR NWQ", SaveChip::Eeprom512, 3},
    {"NB6", SaveChip::Eeprom512, 5},
    {"NML NPT", SaveChip::Eeprom512, 6},
    {"NCG", SaveChip::Eeprom512, 7},
    {"NCV NDP NT9", SaveChip::FlashGeneric, 0},
    {"NDA", SaveChip::FlashGeneric, 1},
    {"NKJ NMQ NRH NSQ", SaveChip::FlashGeneric, 2},
    {"NW4", SaveChip::FlashGeneric, 3},
    {"CP2 NP3 NPO", SaveChip::FlashGeneric, 4},
    {"NPF NPN", SaveChip::FlashMx29l1100, 0},
    {"NCC NCK NJF NM6 NZS", SaveChip::FlashMx29l1100, 2},
    {"NAF", SaveChip::FlashMx29l1100, 9},
    {"N7I N8I N8W N9F NAI NAY NB9 NBJ NBO NBU NDH NDM NDS NET NF9 NFS NGN NGS NH5 NHG NHN NHS NHX "
     "NJ2 NJ3 NJE NJL NJP NKM NKR NM9 NMA NMJ NMM NNM NNR NOM NOW NPC NPK NPL NPU NRK NRT NS2 NSG "
     "NSH NSO NST NSY NTO NTS NTT NTU NVC NVR NW8 NWG NWS NX2 NXG NY2",
     SaveChip::None, 1},
    {"NCB NDF NJQ NKE NM3 NMT NRG NWF", SaveChip::None, 2},
    {"N22 N2M N2P N2V N32 N3P N4W N64 N8M N9B N9C N9H N9M NAC NAH NAM NAR NAS NB2 NB3 NB4 NB8 NBA "
     "NBE NBF NBI NBL NBP NBQ NBR NBS NBW NBX NBY NBZ NCD NCE NCL NCO NCS NDC NDE NDN NDT NDW NDZ "
     "NEG NFB NFD NFF NFL NFO NFQ NFR NG2 NG5 NGA NGB NGD NGM NGR NGX NH9 NHC NHK NHL NHM NHO NHT "
     "NHV NHW NIS NIV NJA NKK NL2 NLC NLG NM4 NMB NMD NMY NN2 NNC NNL NNS NO7 NOF NP9 NPB NPR NPX "
     "NPZ NQ2 NQ8 NQ9 NQB NQC NQK NR2 NR3 NR6 NRD NRO NRP NRR NRU NRV NRW NSB NSD NSF NSK NSL NSP "
     "NSX NSZ NT2 NT4 NTA NTD NTF NTH NTI NTK NTQ NV2 NV3 NV8 NVG NW3 NWB NWD NWI NWK NWM NWN NWO "
     "NWP NWV NWW NWZ NX3 NXC NXF NYP NZO",
     SaveChip::None, 3},
    {"NOH", SaveChip::None, 6},
    {"NOB", SaveChip::Sram32768, 0},
    {"NGP NJ5 NP4 NPE NPM NRI NSI NYW", SaveChip::Sram32768, 1},
    {"CFZ CZL NAL NB5 NFZ NG6 NIB NJG NRE NTE NVB NW2 NWL NZL", SaveChip::Sram32768, 2},
    {"NA2 NHY NKG NPS NT3 NVP NWX", SaveChip::Sram32768, 3},
    {"CPS", SaveChip::Sram32768, 4},
    {"NP6 NPA NS4", SaveChip::Sram32768, 5},
    {"NMF NUM", SaveChip::Sram32768, 6},
    {"NUT", SaveChip::Sram32768, 7},
    {"CDZ", SaveChip::Sram98304, 2},
};

} // namespace cupid::n64::cartridge
