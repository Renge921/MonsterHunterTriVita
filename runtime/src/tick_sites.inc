// Tick sites for RMHJ08: guest loads of step counters and countdowns (recomp.yml
// translation.tick_sites_path; tick_sites.cpp). {pc, kind[, limit[, module]]},
// sorted by pc. A module ("demo", "net_data", "quest_field") marks a site in
// addresses a boot module and an overlay share: only that module's translation
// hooks it (scripts/tick_sites_filter.py).
{0x80039E78u, TickSite::Countup},                    // enemy part (800FB1C4): steps (+0x10)
{0x8003A0F0u, TickSite::Countup, 9},                 // enemy part: steps (+0xC), up to 10
{0x8003B798u, TickSite::CountdownPastZero},          // effect object (800FC48C): wait, then life (+0xC)
{0x8003B8A0u, TickSite::Countup},                    // effect object (800FC48C): age (+0x10) for its texture matrix curves
{0x8003E1F0u, TickSite::Countup},                    // effect spawner (800FF938): per-slot steps since the last spawn (+0xE8)
{0x800451C8u, TickSite::CountdownPastZero},          // effect (801E3E68, ending): steps left (+0x10) before the release
{0x800451D4u, TickSite::CountdownPastZero},          // enemy effect (80106594, ending): steps left (+0x10) before the release
{0x80045764u, TickSite::Countdown},                  // effect object (80106984): delay (+0xC)
{0x80046FE0u, TickSite::Countdown},                  // hunter effect (80107EC0, kind 0xC): start delay (+0x10)
{0x80048000u, TickSite::CountdownPastZero},          // hunter effect (80109458): life (+0xC), ends below 0
{0x8004886Cu, TickSite::CountdownPastZero},          // hunter effect (80109AE8): steps to the next spark (+0xC), every 20
{0x80048E80u, TickSite::CountdownPastZero},          // hunter effect (80109D64, state 5): wait (+0x10)
{0x8004902Cu, TickSite::CountdownPastZero},          // hunter effect (8010A238, ending): steps left (+0x10) before the release
{0x80049228u, TickSite::CountdownPastZero},          // object (8010A500): countdown (+0x24)
{0x80049234u, TickSite::CountdownPastZero},          // object: countdown (+0x1C), fires below 0
{0x800492E4u, TickSite::CountdownPastZero},          // object: countdown (+0x20), fires below 0
{0x8004B90Cu, TickSite::PerStep},                    // joint effect (8010C95C): spin (+0x28) 2048 per step
{0x8004B9F8u, TickSite::Countup},                    // joint effect: 32-step cycle (+0xC)
{0x8004BA20u, TickSite::Countdown},                  // joint effect: steps to the next sound (s16 +0x4C), every 45
{0x8004E3F8u, TickSite::CountdownPastZero},          // hunter weapon effect: steps to the next burst (+0x10) in the motion window
{0x8004E4DCu, TickSite::CountdownPastZero},          // hunter weapon effect: steps to the next spark (s8 +0x67), every 13
{0x8004EA30u, TickSite::CountdownPastZero},          // hunter weapon effect: steps to the next spark (s8 +0x67), every 13
{0x8004EAF4u, TickSite::Countup},                    // hunter weapon effect: steps since the start (+0xC)
{0x8004F900u, TickSite::Countup},                    // hunter effect (80110A7C): steps (+0xC), next stage after 60 or 20
{0x8004F908u, TickSite::Countup},                    // hunter effect (80110A7C): steps (+0x10)
{0x8004F994u, TickSite::PerStep},                    // hunter effect (80110A7C, falling): fall acceleration (x the steps) per step
{0x8004F9F0u, TickSite::PerStep},                    // hunter effect falling: fall speed (+0x6C) moved per step
{0x8004FCC4u, TickSite::PerStep},                    // hunter effect (80110A7C, landing): fall acceleration per step
{0x8004FD20u, TickSite::PerStep},                    // hunter effect landing: fall speed (+0x6C) moved per step
{0x80053590u, TickSite::Countdown},                  // hunter effects (kind 32): life, then fade (+0xC)
{0x800535F4u, TickSite::Countup},                    // hunter effects (80112D80, kinds 0-2, 28, 57): age (+0x10) for colour keys
{0x80053624u, TickSite::CountupEvery, 30},           // hunter effects (kind 32): age (+0x10), a part every 30
{0x80053678u, TickSite::CountdownPastZero},          // hunter effects (kind 47): life (+0xC)
{0x800536ACu, TickSite::Countup},                    // hunter effects (kind 56): age (+0x10) for colour keys
{0x800537C0u, TickSite::Countup},                    // hunter effects (kind 102): age (+0x10) for colour keys
{0x80053A1Cu, TickSite::Countup},                    // hunter effects (kind 19): age (+0x10) for colour keys
{0x80053A40u, TickSite::Countup},                    // hunter effects (kind 136): age (+0x10)
{0x80058030u, TickSite::Countdown},                  // proximity glow: fade-out key frame (+0x10), down to 0
{0x80058074u, TickSite::Countup},                    // proximity glow (8011A3E8): fade-in key frame (+0x10), up to 10
{0x80059BFCu, TickSite::CountdownPastZero},          // creature (8031DF1C): countdown (s16 +0x30)
{0x8005AD64u, TickSite::Advance, 13},                // proximity glow (8011A900): alpha (u8 +0xB), +13 per step
{0x8005AD70u, TickSite::Countdown},                  // proximity glow: fade-in steps left (+0xC)
{0x8005ADA0u, TickSite::Advance, -5},                // proximity glow: alpha (u8 +0xB), -5 per step
{0x8005ADACu, TickSite::Countdown},                  // proximity glow: dimming steps left (+0xC)
{0x8005ADF8u, TickSite::Advance, -3},                // proximity glow: alpha (u8 +0xB), -3 per step
{0x8005AE04u, TickSite::Countdown},                  // proximity glow: fade-out steps left (+0xC)
{0x8005C928u, TickSite::CountdownPastZero},          // effect object (800FC48C): wait (+0xC) before its burst
{0x8005D5A0u, TickSite::CountdownPastZero},          // volcano object (8011CF28): countdown (+0xC), fires below 0
{0x8005D898u, TickSite::Countdown},                  // volcano object: countdown (+0xC), fires at 0
{0x8005E790u, TickSite::Countup},                    // hunter effect (kind 0x10): colour key frame (+0x10)
{0x80064D40u, TickSite::CountdownPastZero},          // Alatreon pieces (802F2D14): start delay (+0x10)
{0x80064D5Cu, TickSite::CountdownPastZero},          // Alatreon pieces: wait (+0xC) between pieces, 3 ticks
{0x80065068u, TickSite::PerStep},                    // Alatreon pieces: growth per step
{0x800650E0u, TickSite::Countup},                    // Alatreon pieces: colour phase (+8)
{0x80065100u, TickSite::Countup},                    // Alatreon pieces: colour phase (+8)
{0x80065120u, TickSite::Countup},                    // Alatreon pieces: colour phase (+8)
{0x80065140u, TickSite::Countup},                    // Alatreon pieces: colour phase (+8)
{0x80065284u, TickSite::Countup},                    // Alatreon effect (802F3254): life (+0xC)
{0x80065400u, TickSite::CountupEvery, 32},           // Alatreon effect (802F33BC): age (+0xC) for its curves, kind 6 == 32 once
{0x80066CB0u, TickSite::Countup},                    // glowing parts (802FA3A0): colour pulse phase (s16 +0x30A per part), 120-step cycle
{0x80066D68u, TickSite::Countdown},                  // glowing parts: near fade (+0xC) down to 0
{0x80066D80u, TickSite::Countup, 14},                // glowing parts: near fade (+0xC) up to 15
{0x8006868Cu, TickSite::CountdownPastZero},          // enemy (80104220): countdown (+0xC), fires below 0
{0x8006B858u, TickSite::Advance, 5461},              // effect object (80307EA8): spin (+0x2C)
{0x8006B9D8u, TickSite::Advance, 5461},              // effect object (80307EA8): spin (+0x28)
{0x8006BAECu, TickSite::CountdownPastZero},          // debris (80308960): life (+0xC), ends below 0
{0x8006BB30u, TickSite::PerStep},                    // debris (80308960): gravity (+0x18) off the fall speed each step
{0x8006BB44u, TickSite::Advance, -2185},             // debris (80308960): spin (+0x24), -2185 per step
{0x8006BBF4u, TickSite::CountdownPastZero},          // effect object (80308A94): life (+0xC)
{0x8006BC30u, TickSite::Countup},                    // effect object (80308A94): age (+0x10) for its texture matrix curves and colours
{0x8006CC20u, TickSite::Countup},                    // enemy-attached model (8031E858): key frame (+0xC) of its scale curve
{0x8006DA5Cu, TickSite::Advance, 512},               // marker effect (8031F574): bob phase (u16), +512 per step
{0x8006DA68u, TickSite::Advance, 255},               // marker effect: spin angle (+0x28), +255 per step
{0x8006F18Cu, TickSite::PerStep},                    // fish (8034062C, appearing): scale x grown per step (target / 10)
{0x8006F1B4u, TickSite::PerStep},                    // fish appearing: scale y grown per step
{0x8006F1C8u, TickSite::PerStep},                    // fish appearing: scale z grown per step
{0x8006F1DCu, TickSite::Countdown},                  // fish appearing: steps left (+0xC), then the full scale
{0x8006FAD4u, TickSite::PerStep},                    // fish (80340D14, at a bait): speed (+0x20) to the bait moved per step
{0x8006FB10u, TickSite::Countdown},                  // fish at a bait: steps to the bait left (+0xC)
{0x80070080u, TickSite::PerStep},                    // fish at a bait: lunge speed (+0x20) moved per step
{0x800700CCu, TickSite::PerStep},                    // fish at a bait: lunge speed change (+0x2C) per step
{0x8007019Cu, TickSite::Countdown},                  // fish at a bait: wait steps left (+0xC)
{0x80070238u, TickSite::Countdown},                  // fish hooked: steps before it turns away (s8 +0x5E)
{0x80070244u, TickSite::CountdownPastZero},          // fish hooked: turn steps left (+0x10), then it faces the bait
{0x800703BCu, TickSite::PerStep},                    // fish leaving a bait: speed (+0x20) moved per step
{0x80070408u, TickSite::PerStep},                    // fish leaving a bait: speed change (+0x2C) per step
{0x800705FCu, TickSite::Countdown},                  // fish (803419F4, on the line): steps to the next tug (+0xC)
{0x80070820u, TickSite::Countdown},                  // fish (80341BD0, hidden): wait steps left (+0xC)
{0x80070A4Cu, TickSite::CountdownPastZero},          // fish (80341DAC, fleeing): steps left (+0xC)
{0x80070AB4u, TickSite::PerStep},                    // fish fleeing: speed (+0x20) moved per step
{0x80070B10u, TickSite::PerStep},                    // fish fleeing: scale shrunk per step (last 10)
{0x80070B48u, TickSite::CountdownPastZero},          // fish fled: steps left (+0xC) before it hides
{0x80072A5Cu, TickSite::Countdown},                  // creature (80358BA4): frame hold (s8 +0x18), next frame at 0
{0x80072B48u, TickSite::CountdownPastZero},          // creature: countdown (s16 +0x14)
{0x8007475Cu, TickSite::Countup},                    // desert object (803677C4): steps (+0x40) per part
{0x80075368u, TickSite::Countup},                    // desert object (80368A94): steps (+0x40) per part
{0x8007593Cu, TickSite::Countup},                    // desert object (803692F0): steps (+0x40) per part
{0x8007617Cu, TickSite::Countup},
{0x80077548u, TickSite::Countup},                    // village effects (80393B50): item age (s16) to 1350
{0x8007756Cu, TickSite::Countdown},                  // village effects: wait (s16) between parts, 8 ticks
{0x80078128u, TickSite::Countup},                    // village effect (80396074): age (+0x10) to its life (+0xC)
{0x8007FFE4u, TickSite::PerStep},                    // flying enemy (8014F654): y (+0x18C) += 10 a step while below the target
{0x8007FFF0u, TickSite::Countdown},                  // flying enemy: countdown (+0x20, from 300)
{0x800BF4FCu, TickSite::Countdown},                  // actor action (80332A40): input cooldown (u8 +7)
{0x800BF5ECu, TickSite::Countdown},                  // actor action (803321FC): input cooldown (u8 +7, 3 steps)
{0x80102808u, TickSite::Countup, 449},               // volcano object (8037F5C8): steps (s16 +0x35A), up to 450
{0x801052F8u, TickSite::PerStep},                    // JPN VolcanoCreatureStep (0x80105254): inlined CreatureFade rate 1/60 (f24)
{0x80105320u, TickSite::PerStep},                    // JPN VolcanoCreatureStep (0x80105254): inlined CreatureFade rate 1/30 (f28)
{0x80105460u, TickSite::Countup, 59},                // volcano creature (80382374): colour fade (u8 +0x2C per part), up to 60
{0x80105488u, TickSite::Countup},                    // volcano creature: steps (s16 +0x8 per part), against 120
{0x80105574u, TickSite::Countup, 179},               // volcano creature: colour fade, up to 180
{0x8010565Cu, TickSite::Countup},                    // volcano creature: steps, against 300
{0x80105748u, TickSite::Countup, 179},
{0x80105814u, TickSite::Countup},                    // volcano creature: steps, against 180
{0x80105900u, TickSite::Countup, 179},
{0x80114EBCu, TickSite::Countdown},                  // field object (step 8032C574 in the handler table 805DE1A0): random wait (s16 +0x22) of 30..329 steps before its next state
{0x80118888u, TickSite::Countdown},                  // small monster (8034F588): s16 timer (+0x32E) in state 1
{0x801188A0u, TickSite::Countdown},                  // small monster: s16 timer (+0x330) in state 0
{0x801188CCu, TickSite::Countdown},                  // small monster: s16 timer (+0x332)
{0x80118A08u, TickSite::Countdown},                  // small monster (8034FA1C): wait (+0x20), acts at 0
{0x8011968Cu, TickSite::Countdown},                  // small monster (80350B34): wait (+0x20), acts at 0
{0x80119AF0u, TickSite::Countup},                    // small monster (80350DD8): state steps (+0x20)
{0x80119F78u, TickSite::Countup},                    // small monster: state steps (+0x20), against 7
{0x8011AD08u, TickSite::Countup},                    // small monster (80351FAC): state steps (+0x20)
{0x8011CC40u, TickSite::Countup},                    // small monster (80353EBC): state steps (+0x20)
{0x8011D000u, TickSite::Countup},                    // small monster (803542A4): state steps (+0x20)
{0x8011D63Cu, TickSite::Countup},                    // small monster (8035455C): state steps (+0x20)
{0x8011D7A4u, TickSite::Countup},                    // small monster (8035455C): state steps (+0x20)
{0x8011DEECu, TickSite::Countup},                    // small monster (80354FFC): state steps (+0x20)
{0x8011EE14u, TickSite::Countup},                    // small monster (80355A44): state steps (+0x20)
{0x8012379Cu, TickSite::Countdown},                  // enemy action (801A14B8): countdown (+0x20), fires at 0
{0x80123EE8u, TickSite::Countdown},                  // enemy action (801A13E8): countdown (+0x20), fires at 0
{0x8012CEACu, TickSite::PerStep},                    // enemy (801A97AC): f32 ramp (+0x34C) down by a constant a step
{0x8012CF10u, TickSite::PerStep},                    // enemy: the ramp up
{0x8012CF2Cu, TickSite::PerStep},                    // enemy: the ramp down
{0x8012CF38u, TickSite::Countup},                    // enemy (801A97AC): steps (s16 +0x350), up to 180
{0x8012CF90u, TickSite::PerStep},                    // enemy: the ramp up
{0x8012E7F4u, TickSite::Countdown},                  // enemy action (801AC674): countdown (+0x20), ends the action at 0
{0x80133064u, TickSite::Countdown},                  // enemy action (801AC930): countdown (+0x20), fires at 0
{0x80136A0Cu, TickSite::Countdown},                  // small monster (801AFE24): wait (s16 +0x22) before a new roll
{0x8014EA74u, TickSite::Countup, 899},               // shared object (80343F50, from ObjectAgesUpdate 80345188): steps since its last sync message (s16 +6); at 900 it resyncs
{0x8014FF40u, TickSite::Countup, 32766},             // JPN ObjectAgesUpdate inlined ObjectAgesTick (8014FE9C): age +0x34 up to 0x7FFF
{0x8014FF54u, TickSite::Countup, 32766},             // JPN ObjectAgesUpdate inlined ObjectAgesTick (8014FE9C): age +0x1C up to 0x7FFF
{0x8014FF70u, TickSite::Countup, 32766},             // JPN ObjectAgesUpdate inlined ObjectAgesTick (8014FE9C): age +0x1E (0..0x7FFF)
{0x801520ACu, TickSite::Countdown},                  // object (80346AF0): countdown (s16 +0x20), fires at 0
{0x801522A0u, TickSite::Countup},                    // object: steps (s16 +0x22)
{0x801522B0u, TickSite::Countup},                    // object: steps (s16 +0x22), against 600
{0x8015C04Cu, TickSite::Countup},                    // enemy (801249F0): steps (s16 +0x914) in state 11
{0x8015C1F0u, TickSite::Countdown},                  // enemy: countdown (+0x20), fires at 0
{0x8015C224u, TickSite::Countdown},
{0x8015C23Cu, TickSite::Countdown},
{0x8016AA9Cu, TickSite::Countup},                    // enemy (80130808): steps (s16 +0x442) against a table length
{0x8016B118u, TickSite::Countdown},                  // enemy senses (80130DC4): countdown (s16 +0x8A4), clamped at 0
{0x8016C37Cu, TickSite::Countdown},                  // enemy senses (80131CFC): four countdowns (s16 +0x444, +0x446, +0x448, +0x44A), each acting on +0x38F at 0
{0x8016C3B0u, TickSite::Countdown},
{0x8016C3DCu, TickSite::Countdown},
{0x8016C408u, TickSite::Countdown},
{0x8016CA70u, TickSite::Countdown},                  // enemy senses (8013234C): countdown (s16 +0x950)
{0x8016E18Cu, TickSite::Countdown},                  // enemy senses (80133660): countdown (s16 +0x952)
{0x8016E304u, TickSite::Countdown},                  // enemy senses (80133784): countdown (s16 +0x94A), 2700 from 0x8013371C
{0x80170414u, TickSite::PerStep},                    // flying enemy (80134E8C): vertical speed (+0x314) += 0.85 a step, below 100
{0x801704DCu, TickSite::PerStep},                    // enemy (80135518): vertical speed (+0x314) += acceleration (+0x320) a step
{0x801704E8u, TickSite::PerStep},                    // flying enemy (80134F7C): the same toward -10
{0x80172CD0u, TickSite::Countdown},                  // enemy senses (80136D84): countdown (s16 +0x79C)
{0x801748B8u, TickSite::PerStep},                    // enemy update (801382A0): a small height offset (+0x1AC) eases to 0 by 10 a step
{0x801748F4u, TickSite::Decay},                      // enemy update: a large height offset (+0x1AC) *= 0.9 a step
{0x80174CC0u, TickSite::Countdown},                  // enemy (80138BC4): countdown (s16 +0x764)
{0x80174CE8u, TickSite::Countdown},
{0x8018E3FCu, TickSite::Countup},                    // JPN AreaStepCounters inlined (0x8073B340)
{0x8018E404u, TickSite::Countup},                    // JPN AreaStepCounters inlined (0x8073B2C4)
{0x8018E408u, TickSite::Countup},                    // JPN AreaStepCounters inlined (0x8073B130)
{0x801993E0u, TickSite::Countdown},                  // player-side timer (802EF294): +0x5C, set from seconds * fps by 802EF3A4
{0x801999D0u, TickSite::Countup},                    // JPN AreaStepCounters inlined (0x8073B340)
{0x801999D8u, TickSite::Countup},                    // JPN AreaStepCounters inlined (0x8073B130)
{0x801999E4u, TickSite::Countup},                    // JPN AreaStepCounters inlined (0x8073B2C4)
{0x8019E41Cu, TickSite::CountupEvery, 32},           // FieldOnlineLoad (8028C730): steps waiting for the members (s16 +6), a sync message every 32
{0x8019E938u, TickSite::Countdown},                  // FieldOnlinePlay (8028CE14): input lock (s16 +4) of 30 steps at the online quest's start
{0x801B2890u, TickSite::Countdown},                  // 8035AF48: countdown (+8), blinks below 30
{0x801EAFB8u, TickSite::Countdown},                  // 0x806C4898 countdown (80383A50), set from seconds * fps by 80383C0C / 80384388
{0x801ED808u, TickSite::Countup},                    // quest result screen (803975D0): +0x2FF4 step counter
{0x801ED878u, TickSite::Countdown},                  // quest result screen: +0x2FF4 countdown, set from seconds * fps by 803973A8
{0x801EE424u, TickSite::Countup, 40},                // JPN quest result: +0x3034 step counter (0..40, resets at 41)
{0x801EE448u, TickSite::Countup, 12},                // JPN quest result: +0x3036 step counter (0..12, sets 0xFFFF at 13)
{0x801F53E8u, TickSite::Countup},                    // quest result screen (8039E3AC): +0x2FF4 step counter
{0x801F5458u, TickSite::Countdown},                  // quest result screen: +0x2FF4 countdown, set from seconds * fps by 8039D174
{0x801FD0E8u, TickSite::CountupEvery, 32},           // JPN QuestStep quest work+0x28 (% 32 == 0)
{0x801FD1D4u, TickSite::CountupEvery, 32},           // JPN QuestStep quest work+0x28 (% 32 == 0)
{0x801FD390u, TickSite::Countup},                    // quest (803B0E28): quest work +0x697C steps, against seconds * fps
{0x801FD5A0u, TickSite::Countup},                    // quest (803B107C): quest work +0x697C steps
{0x801FD650u, TickSite::Countup},
{0x801FD6F8u, TickSite::Countup},
{0x801FD7E0u, TickSite::Countup},
{0x80206278u, TickSite::Countup},                    // quest (803B7D4C): +0x65D4 steps, against seconds * fps
{0x80206C8Cu, TickSite::Countup},                    // quest (803B8718): +0x10 steps, counted while below seconds * fps
{0x80206CCCu, TickSite::Countup},                    // (803B8718): +0x14
{0x80207F3Cu, TickSite::Countdown},                  // ambient creature (803B9AE8): countdown (+0x1B8), clamped at 0
{0x80207F60u, TickSite::Countup},                    // ambient creature: steps (+0x10)
{0x80209B74u, TickSite::Rate},                       // ambient creature glow: pulse (+0x1B0) += rate (u16 +0x1B4) a step
{0x80209B78u, TickSite::Advance, -1024},             // ambient creature glow state (803BB7F8): phase (+0x1A4), -1024 per step
{0x80209D68u, TickSite::Countdown},                  // ambient creature (803BB984): countdown (s16 +0xE), clamped at 0
{0x80209E44u, TickSite::Countdown},                  // ambient creature (803BB984): idle wait (s16 +0xC), fires at 0
{0x80209E70u, TickSite::CountupEvery, 32},           // ambient creature: steps (s16 +0x220), a sound every 32
{0x80209FDCu, TickSite::Countdown},                  // ambient creature: countdown (+0x10), fires at 0
{0x8020A0C4u, TickSite::CountdownPastZero},          // ambient creature: countdown (s16 +0xE), fires below 0
{0x8020A224u, TickSite::Countdown},                  // ambient creature (803BBDA4): wait (s16 +0x220) before a 1-in-2 roll
{0x8020A388u, TickSite::Countdown},                  // ambient creature: countdown (s16 +0xE), clamped at 0
{0x8020A438u, TickSite::CountdownPastZero},          // ambient creature: countdown (+0x10), fires below 0
{0x8020A4F0u, TickSite::Countdown},                  // ambient creature: idle wait (s16 +0xC) in state 10, fires at 0
{0x8020A54Cu, TickSite::Countdown},                  // ambient creature: idle wait (s16 +0xC) in state 129
{0x8020A674u, TickSite::CountdownPastZero},          // ambient creature: countdown (s16 +0xE), fires below 0
{0x8020A67Cu, TickSite::CountdownPastZero},          // ambient creature: countdown (+0x10) while leaving
{0x8020A7E0u, TickSite::CountdownPastZero},          // ambient creature: countdown (+0x10), fires below 0
{0x8021BE8Cu, TickSite::Countdown},                  // village object (80221410) countdown (s16 +0x28), while != 0
{0x80245928u, TickSite::Countdown},                  // NPC timed move (801FDE58, inlined in NpcScriptStep): steps left (s16 +0x1E2)
{0x8024593Cu, TickSite::PerStep},                    // NPC timed move: velocity (+0x1F8, +0x1FC, +0x200) added to the position per step
{0x80245948u, TickSite::PerStep},
{0x80245950u, TickSite::PerStep},
{0x802479ACu, TickSite::Countup},                    // NPC script (NpcScriptStep 801FFD04): steps in the entry (s16 +0x20A); a -2 entry moves on after 3
{0x802479E0u, TickSite::Countup},                    // NPC script: steps in the entry against its length (+0x10)
{0x80249368u, TickSite::Advance, 512},               // village NPC (802014C8): yaw (LB_NPC+0x2C) turning, +512 per step
{0x8024AFB8u, TickSite::Countdown},                  // village NPC (802036C8): wait (+0xC) before the next idle move
{0x8024B2C8u, TickSite::Countdown},                  // village NPC (80203A88): wait (+0xC) before the next idle move
{0x8024C624u, TickSite::Countdown},                  // NPC (80205380): steps (+0xC) before the next motion
{0x8024CC0Cu, TickSite::Countdown},                  // cutscene NPCs (802061F0 .. 802072E8): steps (+0xC) before each next motion
{0x8024CF88u, TickSite::Countdown},                  // NPC (80205DB8): steps (+0xC) before the next motion
{0x8024D088u, TickSite::Countdown},
{0x8024D134u, TickSite::Countdown},                  // farm's first-visit cutscene, Felyne (NpcFarmIntroFelyne 802060D8): steps (+0xC) to the hat wave, then of it
{0x8024D17Cu, TickSite::Countdown},
{0x8024D2ACu, TickSite::Countdown},
{0x8024D370u, TickSite::Countdown},
{0x8024D898u, TickSite::Countdown},
{0x8024D944u, TickSite::Countdown},
{0x8024DA34u, TickSite::Countdown},
{0x8024DA74u, TickSite::Countdown},
{0x8024DB44u, TickSite::Countdown},
{0x8024DB84u, TickSite::Countdown},
{0x8024DBB8u, TickSite::Countdown},
{0x8024DC00u, TickSite::Countdown},
{0x8024DD14u, TickSite::Countdown},
{0x8024DE38u, TickSite::Countdown},
{0x8024DE6Cu, TickSite::Countdown},
{0x8024DF24u, TickSite::Countdown},
{0x8024DF58u, TickSite::Countdown},
{0x802B8918u, TickSite::Countdown},                  // sound handle (800DE3D8): life (s16 +0x48), stops the sound at 0
{0x802B8B68u, TickSite::Countdown},
{0x802B8D10u, TickSite::Countdown},
{0x802B8EA4u, TickSite::Countdown},
{0x802B9030u, TickSite::Countdown},
{0x802B91E8u, TickSite::Countdown},
{0x802B9378u, TickSite::Countdown},
{0x802B9508u, TickSite::Countdown},
{0x802B9698u, TickSite::Countdown},
{0x802B9828u, TickSite::Countdown},
{0x802B9938u, TickSite::Countdown},
{0x802B9A30u, TickSite::Countdown},
{0x802B9BB0u, TickSite::Countdown},
{0x802B9C90u, TickSite::Countdown},
{0x802B9D4Cu, TickSite::Countdown},                  // sound (800DE3D8): countdown (s16), clamped at 0
{0x802B9E10u, TickSite::Countdown},
{0x802B9ED0u, TickSite::Countdown},
{0x802B9F18u, TickSite::Countdown},
{0x802BA010u, TickSite::Countdown},
{0x802BA188u, TickSite::Countdown},
{0x802BA318u, TickSite::Countdown},
{0x802BA4B0u, TickSite::Countdown},
{0x802BA648u, TickSite::Countdown},
{0x802BA7E4u, TickSite::Countdown},
{0x802BA974u, TickSite::Countdown},
{0x802BAB04u, TickSite::Countdown},
{0x802BAC94u, TickSite::Countdown},
{0x802BAE24u, TickSite::Countdown},
{0x802BAFB4u, TickSite::Countdown},
{0x802BB154u, TickSite::Countdown},
{0x802BB2F0u, TickSite::Countdown},
{0x802BB4CCu, TickSite::Countdown},
{0x802CB9D8u, TickSite::Countdown},
{0x802CE4ACu, TickSite::Countdown},
{0x802CE5F8u, TickSite::Countdown},
{0x802CE708u, TickSite::Countdown},
{0x802CE98Cu, TickSite::Countdown},
{0x802CEB78u, TickSite::Countdown},
{0x802CEC88u, TickSite::Countdown},
{0x802CEDE8u, TickSite::Countdown},                  // Cha-Cha (AINPC_W 0x806BF7E0) actions: steps (+0x1AC; s16 +0x22C, +0x232) of a wait or a timeout
{0x802CF5A4u, TickSite::Countdown},
{0x802CF76Cu, TickSite::Countdown},
{0x802CFA28u, TickSite::Countdown},
{0x802CFB58u, TickSite::Countdown},
{0x802CFD58u, TickSite::Countdown},
{0x802D0078u, TickSite::Countdown},
{0x802D02D0u, TickSite::Countdown},
{0x802D048Cu, TickSite::Countdown},
{0x802D0668u, TickSite::Countdown},
{0x802D08E4u, TickSite::Countdown},
{0x802D231Cu, TickSite::Countdown},
{0x802D2498u, TickSite::Countdown},
{0x802D2E9Cu, TickSite::Countdown},
{0x802D3068u, TickSite::Countdown},
{0x802D30C4u, TickSite::Countdown},
{0x802D3828u, TickSite::Countdown},
{0x802D39FCu, TickSite::Countdown},
{0x802D3D74u, TickSite::Countdown},
{0x802D4354u, TickSite::Countdown},
{0x802DC1FCu, TickSite::Countdown},                  // Cha-Cha timers (802D371C, each step): cooldowns while > 0
{0x802DC210u, TickSite::Countdown},
{0x802DC224u, TickSite::Countdown},
{0x802DC238u, TickSite::Countdown, 1},               // Cha-Cha: +0x352, tested == 1
{0x802DC260u, TickSite::Countdown},
{0x802DC274u, TickSite::Countdown},
{0x802DC288u, TickSite::Countdown},
{0x802DC29Cu, TickSite::Countdown},
{0x802DC2B0u, TickSite::Countdown},
{0x802DC2FCu, TickSite::Countdown},
{0x802DC310u, TickSite::Countdown},
{0x802DC324u, TickSite::Countdown},
{0x802DC348u, TickSite::Countdown},
{0x802DC37Cu, TickSite::Countdown},
{0x802DC39Cu, TickSite::Countdown},
{0x802DC3C4u, TickSite::Countdown},
{0x802DC3D8u, TickSite::Countdown},
{0x802DC3ECu, TickSite::Countdown},
{0x802DC400u, TickSite::Countdown},
{0x802DC414u, TickSite::Countdown},
{0x802DC428u, TickSite::Countdown},
{0x802DC43Cu, TickSite::Countdown},
{0x802DC450u, TickSite::Countdown},
{0x802DC464u, TickSite::Countdown},
{0x802DC478u, TickSite::Countdown},
{0x80310FA0u, TickSite::Countup},                    // player idle (80246BE0): steps waiting (+0x28), the idle motion after 30
{0x80311EF4u, TickSite::PerStep},                    // roll (802479B0): Evade Distance push per step in its frame window
{0x80312DA4u, TickSite::PerStep},                    // ledge drop (80247D28): push off the edge per step
{0x80313150u, TickSite::PerStep},                    // weapon-drawn evade (80248A38): lance push per step (r29 set)
{0x803131CCu, TickSite::PerStep},                    // weapon-drawn evade: switch axe push per step (evade kind 0-1)
{0x803132A4u, TickSite::PerStep},                    // weapon-drawn evade: Evade Distance push per step (great sword, sword and shield, hammer, long sword)
{0x80313318u, TickSite::PerStep},                    // weapon-drawn evade: Evade Distance push per step (bowguns)
{0x8031338Cu, TickSite::PerStep},                    // weapon-drawn evade: Evade Distance push per step (lance, switch axe)
{0x80313408u, TickSite::PerStep},                    // weapon-drawn evade: Evade Distance push per step (evade kind 2-4)
{0x80314F9Cu, TickSite::CountdownPastZero},
{0x80315268u, TickSite::Countup},                    // grilling meat (PlGrillStep 8024AAB8): cook time (s16 +0x5B8) against the item's raw/done/burnt steps
{0x803165C0u, TickSite::Countdown, 1},
{0x803166B4u, TickSite::Countdown},
{0x8031F77Cu, TickSite::PerStep},                    // swim (80253F4C): forward push per step
{0x8031FC7Cu, TickSite::Countdown},                  // swim dash (80254428): steps before the next dash (u8 +6)
{0x8031FD54u, TickSite::PerStep},                    // swim dash: forward push per step
{0x803213DCu, TickSite::PerStep},                    // weapon attack lunge (802556F8): K * cos(...) per step until its frame
{0x80321760u, TickSite::PerStep},                    // weapon attack lunge (80255A50): K * cos(...) per step
{0x803217B4u, TickSite::PerStep},
{0x80321808u, TickSite::PerStep},
{0x80321CB0u, TickSite::PerStep},                    // weapon attack lunge (80255EFC): K * cos(...) per step
{0x80323664u, TickSite::PerStep},                    // weapon attack lunge (8025779C): K * cos(...) per step
{0x80323A78u, TickSite::PerStep},                    // weapon attack lunge (80257BE0): K * cos(...) per step
{0x80323E9Cu, TickSite::Countup},                    // player action (8025810C): steps (s16 +0x3F4), on at 200
{0x803242C8u, TickSite::CountupEvery, 32},           // bound hunter (8025856C): steps (u8 +6), a shake every 32
{0x80324530u, TickSite::Countdown},                  // bound hunter: mash cooldown (u8 +7, 3 steps)
{0x80324574u, TickSite::CountdownPastZero},          // bound hunter: steps left (s16 +0x3FA) before breaking free
{0x803252F8u, TickSite::PerStep},                    // carrying (8025C9D8): walk push per step
{0x803254C4u, TickSite::PerStep},                    // carrying (8025CB2C): dash push per step
{0x8032618Cu, TickSite::PerStep},                    // roll (8025A190): Evade Distance push per step in its frame window
{0x80328478u, TickSite::Countup},                    // player action (8025C2E0): steps (s16 +0x34), on at 150
{0x8032A274u, TickSite::PerStep},                    // player action (8025D720): y lowered per step
{0x80331C04u, TickSite::Countdown},                  // player condition (80264334): countdown (s16 +0x3EE), fires at 0
{0x80331DACu, TickSite::CountdownEvery, 90},         // player condition: countdown (s16 +0x404), tested % 90 == 0
{0x80332004u, TickSite::Countdown},                  // player condition: countdown (s16 +0x462), fires at 0
{0x80338774u, TickSite::PerStep},                    // player move (8026A548, from 80256694): velocity x (+0x78) per step
{0x80338780u, TickSite::PerStep},                    // player move: velocity y (+0x7C) per step
{0x80338788u, TickSite::PerStep},                    // player move: velocity z (+0x80) per step
{0x80338FE8u, TickSite::Countdown},                  // player input (8026AF6C): input lock (s16 +0x352), also counted down by PlayerStepTimers
{0x8034B310u, TickSite::Countdown},                  // knockback (8027AF98): steps left (s16 +0xB4)
{0x8034B32Cu, TickSite::PerStep},                    // knockback: velocity x (+0x9C) per step
{0x8034B338u, TickSite::PerStep},                    // knockback: velocity y (+0xA0) per step
{0x8034B340u, TickSite::PerStep},                    // knockback: velocity z (+0xA4) per step
{0x80352F40u, TickSite::PerStep},                    // sword and shield lunge (80283874): K * cos(...) per step
{0x803588A8u, TickSite::Countup},                    // actor action step counter (+0x28)
{0x80358DDCu, TickSite::Countup},
{0x8035B628u, TickSite::PerStep},                    // lance step (80288ECC): push per step in its frame window
{0x8035D304u, TickSite::Advance, -364},              // bowgun side step (8028AC78): move angle (+0x58), -364 per step
{0x8035D314u, TickSite::Advance, 364},               // bowgun side step: move angle, +364 per step
{0x8035D328u, TickSite::PerStep},                    // bowgun side step: push per step
{0x8035D5ECu, TickSite::Advance, -364},              // bowgun side step (8028AFB4): move angle (+0x58), -364 per step
{0x8035D5FCu, TickSite::Advance, 364},               // bowgun side step: move angle, +364 per step
{0x8035D610u, TickSite::PerStep},                    // bowgun side step: push per step
{0x8035EA04u, TickSite::PerStep},                    // long sword step (80331D68, each step from 80331E58): push per step
{0x8035F510u, TickSite::Countdown},                  // actor action (80332684): input cooldown (u8 +7)
{0x8035F558u, TickSite::CountdownPastZero},          // actor action countdown (+0x28), fires at <= 0
{0x803603D4u, TickSite::Countdown},                  // actor action (803334B4): input cooldown (u8 +7)
{0x80360728u, TickSite::Countdown},                  // actor action (80333740): input cooldown (u8 +7)
{0x803610A4u, TickSite::Countdown},                  // actor action (80334214): input cooldown (u8 +7)
{0x803610ECu, TickSite::CountdownPastZero},
{0x8036E130u, TickSite::EaseDivisor},                // field camera (802B5CBC): height eases, target (f31) and eye (f30). These 9 entries
{0x8036E22Cu, TickSite::EaseDivisor},
{0x8036E230u, TickSite::EaseDivisor},
{0x8036E3B4u, TickSite::Countdown, 1},               // field camera: ease timer (+0xA8) while swimming, down to 1
{0x8036E3C4u, TickSite::EaseDivisor},
{0x8036E3CCu, TickSite::EaseDivisor},
{0x8036E3D4u, TickSite::EaseDivisor},
{0x8036E3F4u, TickSite::Countdown},                  // field camera: ease timer (+0xA8)
{0x8036E400u, TickSite::EaseDivisor},
{0x8036F7E0u, TickSite::Countdown},                  // camera climb mode (802B7880, twice a step): steps before the climb view (s16 +0x86)
{0x8036F814u, TickSite::Countdown},                  // camera climb mode: steps before leaving it
{0x8037015Cu, TickSite::Countup},                    // camera collision (CameraCollide 802B8244): ticks a height correction is held, to the row's limit (s8 +0xA5 while pushed)
{0x80370194u, TickSite::Countup},                    // camera collision: the same (s8 +0xA2)
{0x80370334u, TickSite::Countup},                    // camera collision: ticks a sideways correction is held (+0xA5 while pushed)
{0x80370394u, TickSite::Countup},                    // camera collision: the same (+0xA2)
{0x80370698u, TickSite::Countdown},                  // camera collision: release blends' steps left (u8 +0x102, +0x103; the curve reads the step's time)
{0x8037188Cu, TickSite::PerStep},                    // scope camera (802B988C): zoom (+0x1DC, +0x1E0) by 1.75 a step
{0x80371E50u, TickSite::Countdown},                  // camera offset blend (802B9C68): steps left (s16 +0x234)
{0x80372BB4u, TickSite::Countdown},                  // zone camera angle blend (802BAC0C): steps left (s16 zone +0x44)
{0x80372E44u, TickSite::Countdown},                  // zone camera point blend (802BAE68): steps left (s16 zone +0x44)
{0x80374DE4u, TickSite::CountdownPastZero},          // camera script wait (op 0x25): frames left, done below 0
{0x803758B0u, TickSite::Countdown},                  // camera path hold (802BD78C): steps before playing
{0x803758E8u, TickSite::Countup},                    // camera path frame (802BD78C)
{0x80383FE4u, TickSite::Countdown},                  // stage (802B4B20): countdown (s16 +0x2FDC), fires at 0
{0x80386048u, TickSite::CountdownPastZero},          // object (8038608C): countdown (+0x16C), fires below 0
{0x80386674u, TickSite::Countdown},                  // object (8038642C): countdown (+0x16C), fires at 0
{0x80387A78u, TickSite::Countdown},                  // field actor (803A44AC, wandering): wait (+0x16C) of 100..163 steps
{0x80388D24u, TickSite::Advance, 255},               // online marker effect (803C3574): spin angle (+0x28)
{0x80388D30u, TickSite::Advance, 512},               // online marker effect: bob phase (u16)
{0x80388FB0u, TickSite::Advance, 255},
{0x80388FBCu, TickSite::Advance, 512},
{0x8038CC70u, TickSite::Countup},                    // event demo (803C46C4): demo steps (0x806D4FA4), the demo script's clock
{0x803907A0u, TickSite::Countup},                    // quest NPC talk typing (803C7FEC): steps (+0x10C); shows steps / 2 + 1 letters
{0x8041CDC4u, TickSite::Countdown},                  // name-entry keyboard open (80449B00): input wait (0x8079722C), 15 after opening
{0x8041D284u, TickSite::Countdown},                  // name-entry keyboard update (8044A038): the same input wait
{0x8041FB74u, TickSite::CountdownPastZero},          // name-entry keyboard (8044C72C): close delay (the same word, from 2), fires below 0
{0x809E7494u, TickSite::Countup, 0, "quest_field"},  // quest object (quest_data RSO): steps (+0x4)
{0x809F86B0u, TickSite::Countup, 0, "quest_field"},  // quest object (quest_data RSO): steps (+0x10)
{0x809F9284u, TickSite::Countup, 0, "quest_field"},  // quest object (quest_data RSO): steps (+0x10)
{0x809FA308u, TickSite::Countup, 0, "quest_field"},  // quest object (quest_data RSO): steps (+0x10)
{0x809FAB5Cu, TickSite::Countdown, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x14)
{0x809FABB0u, TickSite::Countup, 0, "quest_field"},  // quest object (quest_data RSO): steps (+0x14)
{0x809FB6D4u, TickSite::Advance, -5461, "quest_field"}, // quest object (quest_data RSO): spin (+0x28), -5461 a step
{0x809FB704u, TickSite::Advance, -546, "quest_field"}, // quest object: spin (+0x24)
{0x809FB708u, TickSite::Advance, -364, "quest_field"}, // quest object: spin (+0x2C)
{0x809FB78Cu, TickSite::Advance, -5461, "quest_field"}, // quest object: spin (+0x28)
{0x809FB8B0u, TickSite::Advance, 128, "quest_field"}, // quest object: phase (u16 +0x2E)
{0x809FBFD8u, TickSite::Countdown, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x10)
{0x809FD178u, TickSite::Countdown, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x10)
{0x809FD214u, TickSite::Countdown, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x10)
{0x809FDBB0u, TickSite::Countup, 0, "quest_field"},  // quest object (quest_data RSO): steps (+0x10)
{0x809FE308u, TickSite::Countup, 0, "quest_field"},  // quest object (quest_data RSO): steps (+0x10)
{0x809FFB78u, TickSite::CountdownPastZero, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x14)
{0x809FFC80u, TickSite::Advance, 1274, "quest_field"}, // quest object: spins (+0x28..+0x30) by kind
{0x809FFC8Cu, TickSite::Advance, 910, "quest_field"},
{0x809FFC9Cu, TickSite::Advance, 455, "quest_field"},
{0x809FFCA8u, TickSite::Advance, 455, "quest_field"},
{0x809FFCB8u, TickSite::Advance, 910, "quest_field"},
{0x809FFCC8u, TickSite::Advance, 546, "quest_field"},
{0x809FFCD4u, TickSite::Advance, 91, "quest_field"},
{0x809FFCE0u, TickSite::Advance, -181, "quest_field"},
{0x809FFCF4u, TickSite::Advance, 2731, "quest_field"},
{0x809FFD04u, TickSite::Advance, 3641, "quest_field"},
{0x809FFD14u, TickSite::Advance, 4915, "quest_field"},
{0x809FFD20u, TickSite::Advance, 4369, "quest_field"},
{0x809FFD2Cu, TickSite::Advance, 1638, "quest_field"},
{0x80A01954u, TickSite::Countup, 0, "quest_field"},  // quest object (quest_data RSO): steps (+0x10)
{0x80A01EB4u, TickSite::Countup, 0, "quest_field"},  // quest object (quest_data RSO): steps (+0x34)
{0x80A01F28u, TickSite::Countup, 0, "quest_field"},  // quest object (quest_data RSO): steps (+0x34)
{0x80A0202Cu, TickSite::CountdownPastZero, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x34)
{0x80A020D8u, TickSite::Countup, 0, "quest_field"},  // quest object (quest_data RSO): steps (+0x34)
{0x80A023D8u, TickSite::Countdown, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x10)
{0x80A025D4u, TickSite::Countup, 0, "quest_field"},  // quest object (quest_data RSO): steps (+0x34)
{0x80A02EC0u, TickSite::CountdownPastZero, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x14)
{0x80A02F58u, TickSite::CountdownPastZero, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x14)
{0x80A03008u, TickSite::CountdownPastZero, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x14)
{0x80A03034u, TickSite::Countup, 0, "quest_field"},  // quest object (quest_data RSO): steps (+0x10)
{0x80A0311Cu, TickSite::CountdownPastZero, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x14)
{0x80A0452Cu, TickSite::Countup, 0, "quest_field"},  // quest object (quest_data RSO): steps (+0x10)
{0x80A05CACu, TickSite::Countup, 0, "quest_field"},  // quest object (quest_data RSO): steps (+0x10)
{0x80A0727Cu, TickSite::Countup, 0, "quest_field"},  // quest object (quest_data RSO): steps (+0x54)
{0x80A07A38u, TickSite::Countdown, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x10)
{0x80A07BCCu, TickSite::CountdownPastZero, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x10)
{0x80A08058u, TickSite::CountdownPastZero, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x58)
{0x80A08064u, TickSite::CountdownPastZero, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x5C)
{0x80A0837Cu, TickSite::CountdownPastZero, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x10)
{0x80A084B0u, TickSite::Countup, 0, "quest_field"},  // quest object (quest_data RSO): steps (+0x58)
{0x80A08B34u, TickSite::Countdown, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x10)
{0x80A099B4u, TickSite::Countup, 0, "quest_field"},  // quest object (quest_data RSO): steps (+0x14)
{0x80A09C88u, TickSite::Countdown, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x10)
{0x80A09D00u, TickSite::Countdown, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x10)
{0x80A09D68u, TickSite::Countdown, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x10)
{0x80A09DE4u, TickSite::Countdown, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x10)
{0x80A09EE4u, TickSite::CountdownPastZero, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x14)
{0x80A0A4D4u, TickSite::Countup, 0, "quest_field"},  // quest object (quest_data RSO): steps (+0x10)
{0x80A0A804u, TickSite::CountdownPastZero, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x10)
{0x80A0A980u, TickSite::Countup, 0, "quest_field"},  // quest object (quest_data RSO): steps (+0x10)
{0x80A0AE30u, TickSite::CountdownPastZero, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x10)
{0x80A0B558u, TickSite::Countdown, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x10)
{0x80A0B684u, TickSite::CountdownPastZero, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x10)
{0x80A0BB24u, TickSite::CountdownPastZero, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x14)
{0x80A0BBD8u, TickSite::Countdown, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x10)
{0x80A0BF6Cu, TickSite::Countup, 0, "quest_field"},  // quest object (quest_data RSO): steps (+0x14)
{0x80A0C1FCu, TickSite::CountdownPastZero, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x10)
{0x80A0C7DCu, TickSite::CountdownPastZero, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x10)
{0x80A0CBD8u, TickSite::CountdownPastZero, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x14)
{0x80A0D544u, TickSite::Countup, 0, "quest_field"},  // quest object (quest_data RSO): steps (+0x10)
{0x80A0DA30u, TickSite::Countdown, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x14)
{0x80A0DB68u, TickSite::Countdown, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x14)
{0x80A0DD74u, TickSite::Advance, 9102, "quest_field"}, // quest object: spin (+0x24)
{0x80A0DD84u, TickSite::Advance, 5461, "quest_field"},
{0x80A0DD94u, TickSite::Advance, 5461, "quest_field"},
{0x80A0E69Cu, TickSite::CountdownPastZero, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x14)
{0x80A11178u, TickSite::Countdown, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x10)
{0x80A11314u, TickSite::CountdownPastZero, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x14)
{0x80A1182Cu, TickSite::CountdownPastZero, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x10)
{0x80A11984u, TickSite::Countup, 0, "quest_field"},  // quest object (quest_data RSO): steps (+0x14)
{0x80A11BA8u, TickSite::Countup, 0, "quest_field"},  // quest object (quest_data RSO): steps (+0x14)
{0x80A120D0u, TickSite::Countdown, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x10)
{0x80A126E4u, TickSite::CountdownEvery, 10, "quest_field"}, // quest object (quest_data RSO): countdown (+0x10), kind 123 at % 10 == 0
{0x80A128D8u, TickSite::Countup, 0, "quest_field"},  // quest object (quest_data RSO): steps (+0x14)
{0x80A12AACu, TickSite::Countup, 0, "quest_field"},  // quest object (quest_data RSO): steps (+0x14)
{0x80A131DCu, TickSite::Countup, 0, "quest_field"},  // quest object (quest_data RSO): steps (+0x10)
{0x80A13590u, TickSite::Countup, 0, "quest_field"},  // quest object (quest_data RSO): steps (+0x14)
{0x80A13B30u, TickSite::CountdownPastZero, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x10)
{0x80A13B6Cu, TickSite::Countup, 29, "quest_field"}, // quest object (quest_data RSO): steps (+0x14)
{0x80A13C44u, TickSite::CountdownPastZero, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x10)
{0x80A13C88u, TickSite::CountdownPastZero, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x10)
{0x80A13D08u, TickSite::Countup, 0, "quest_field"},  // quest object (quest_data RSO): steps (+0x28)
{0x80A13D5Cu, TickSite::Countdown, 0, "quest_field"}, // quest object (quest_data RSO): countdown (+0x14)
{0x80A80B74u, TickSite::Countup},                    // enemy (em_data RSO): steps (+0x14) while +0x43D is 1
{0x80B195E4u, TickSite::Advance, 1024, "demo"},      // title character view (8097DCFC): turn (+0x1142) while the button is held
{0x80B195F8u, TickSite::Advance, -1024, "demo"},
{0x80B19730u, TickSite::Countdown, 0, "net_data"},   // net menu (net_data RSO, 8097F2AC): Felyne frame countdown (+0x12C) from 20, flips +0x130 at 0
{0x80B19B80u, TickSite::Countdown, 0, "net_data"},   // net menu text list (8097F774): arrow bumps (u8 0x80995EBA, +1) from 4
{0x80B19B94u, TickSite::Countdown, 0, "net_data"},
{0x80B19C14u, TickSite::Countup, 11, "net_data"},    // net menu text list: steps a direction is held (+0x2C), scrolls each step from 12
{0x80B1A388u, TickSite::Countdown, 0, "demo"},       // title (demo_title_move 8097EBE4): wait (+4) before the state runs
{0x80B1A464u, TickSite::Countdown, 0, "demo"},       // title: state timer (+6) counting down
{0x80B1A49Cu, TickSite::Countdown, 0, "demo"},
{0x80B1A654u, TickSite::Countup, 899, "demo"},       // title screen: idle steps (+6) until the attract movie at 900
{0x80B1A694u, TickSite::Rate, 0, "demo"},            // title: pulse phase (+0x104C) += rate (+0x1050, 512)
{0x80B1A72Cu, TickSite::Countup, 0, "demo"},         // title: steps (+6) of the fade to the attract movie, 20
{0x80B1A890u, TickSite::Countdown, 0, "demo"},
{0x80B1B1E0u, TickSite::Countdown, 0, "demo"},       // title
{0x80B1B390u, TickSite::Rate, 0, "demo"},
{0x80B1B5F0u, TickSite::Countdown, 0, "net_data"},   // net menu window (8097FFF8): Felyne frame countdown (+0x12C) from 20
{0x80B1B61Cu, TickSite::Countdown, 0, "net_data"},   // net menu window: message cycle countdown (+0x12E) from 150, moves +0x131 on
{0x80B1B640u, TickSite::Countdown, 0, "demo"},
{0x80B1C00Cu, TickSite::Rate, 0, "demo"},            // title
{0x80B1C6BCu, TickSite::Countup, 0, "demo"},         // title: steps (+0x1140) until the menu music starts
{0x80B1C858u, TickSite::Countdown, 0, "demo"},
{0x80B1C938u, TickSite::Rate, 0, "demo"},            // title
{0x80B1CBD4u, TickSite::Countdown, 0, "demo"},
{0x80B1CD6Cu, TickSite::Rate, 0, "demo"},
{0x80B1D004u, TickSite::Countup, 0, "net_data"},     // online_sub_reflect (80980DE4): steps waiting (+0x128), shown as seconds (/ 30)
{0x80B1D05Cu, TickSite::Countup, 0, "net_data"},
{0x80B1D294u, TickSite::Countup, 0, "net_data"},     // online_sub_reflect: steps waiting (+0x128)
{0x80B1D52Cu, TickSite::Rate, 0, "demo"},
{0x80B1E1C8u, TickSite::Countdown, 0, "demo"},
{0x80B1E454u, TickSite::Countup, 0, "demo"},         // title menu: blink steps (+6), mod 30
{0x80B1E834u, TickSite::Rate, 0, "demo"},
{0x80B1EA58u, TickSite::Countdown, 0, "demo"},
{0x80B1EF68u, TickSite::Countdown, 0, "demo"},
{0x80B1F828u, TickSite::Countdown, 0, "demo"},
{0x80B28288u, TickSite::Countdown, 0, "demo"},       // title task (Demo_task 80993470): logo timer (+0xC)
{0x80B282C0u, TickSite::Countdown, 0, "demo"},
