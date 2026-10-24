# Tesla Roadster (2026): reference images and videos

Compiled 2026-10-24. Local copies are in `E:\TeslaRoadsterSim\Research\images\`. Every official image shows the **2026 reveal car in Ano White**.

## A. Official tesla.com/roadster images (downloaded)
The URL pattern is `https://digitalassets.tesla.com/tesla-contents/image/upload/f_auto,q_auto/<NAME>`. Swap `-Desktop.jpg` for `-Mobile.jpg` to get the portrait crops. The local files were fetched with `f_jpg,q_auto`.

| Local file | Size | View / what it shows | Best use |
|---|---|---|---|
| `tesla_official_Roadster-01-Hero-Desktop.jpg` | 4200×1698 | Dead-on **rear** view in a dark studio, wing stowed. Full-width red taillight, "RDSTR" plate, 4 thruster nozzles, diffuser, recessed rear window between sail panels. | Rear elevation, tail proportions |
| `tesla_official_Roadster-02-Pilot-Desktop.jpg` | 3440×1024 | **Near-orthographic left side profile**, doors closed, wing stowed. 6-spoke wheels, "TROFEO" tire script. | **Primary blueprint side view**; dimension scaling |
| `tesla_official_Roadster-03-Carousel-Slide-1-Desktop.jpg` | 2200×1040 | Dead-on **front** view. Full-width front light bar, hidden headlights lit in the fender-corner pockets, hood vent panel, gold-tinted windshield. | Front elevation, width/height ratio |
| `tesla_official_Roadster-03-Carousel-Slide-2-Desktop.jpg` | 2200×1040 | **High rear-corner top-down** close-up. Deck panel seams, wing trailing edge, red light strip, RDSTR plate, nozzle lips, carbon diffuser. | Rear deck surfacing, panel gaps |
| `tesla_official_Roadster-03-Carousel-Slide-3-Desktop.jpg` | 2200×1040 | **High front 3/4, almost top view, both butterfly doors open.** Hood, fenders, canopy, center roof spine. | Door hinge and opening geometry, top plan |
| `tesla_official_Roadster-03-Carousel-Slide-4-Desktop.jpg` | 2200×1040 | **Interior from behind the seats.** Yoke, floating center screen (showing the 3D car UI), suede dash, carbon doors, red ambient strips, white perforated seats, center spine. | Cockpit modeling |
| `tesla_official_Roadster-03-Carousel-Slide-5-Desktop.jpg` | 2200×1040 | Chassis **x-ray render**: rear pushrod/rocker coil-over (gold spring), subframe, Trofeo tire. | Suspension detail |
| `tesla_official_Roadster-04-World-Desktop.jpg` | 2200×1782 | **Rear 3/4 at night**, both doors open, **wing deployed** (swan-neck struts, red wing light), Starman figure inside. Downward lift nozzles near both wheels (Rocket). | Wing-up pose, door-open pose, lighting |
| `tesla_official_Roadster-05-Graph-Desktop.jpg` | 2200×1238 | Official **downforce vs speed graph**: Multi-Mode Fan band, Tire Load Limit, Low Drag curve. Shows the typography style. | Aero model shape; font reference |
| `tesla_official_Roadster-06-Gravity-Desktop.jpg` | 3456×3456 | **Two rear views:** the car **upside down** (top) and right side up firing thrusters (bottom). 4 rear nozzles plus 2 downward corner nozzles, 4 fans visible, wing deployed. | Thruster and fan placement, VFX reference |
| `tesla_official_Roadster-07-Augments-Desktop.jpg` | 3456×2800 | Rear close-up in red light: wing struts, doubled red light bars, 4 nozzles, 4 fans in a carbon box. | Rear-detail texture, emissive layout |
| `tesla_official_Roadster-08-End-Desktop.jpg` | 2800×2074 | **Front 3/4 left** studio shot, doors closed. | Classic beauty and lookdev reference |
| `tesla_official_Roadster-01-Hero-Easter-Egg-Desktop.jpg` | 2200×1262 | Starman in the Roadster over Earth (render), door glass open. | Glass tint and canopy opening |
| Logo `Roadster-Logo-Final-Desktop-Left-Aligned.png` (not downloaded) | 1890×138 | "ROADSTER" wordmark | Badge/UI wordmark |

## B. Patent and detail images (downloaded, via Sawyer Merritt's X post of 2026-10-17)
| Local file | Description |
|---|---|
| `patent_fan_suction_fig_sawyermerritt_1.png` | Patent figure: 4 ducted fans (102) in a rear outlet (104/106) fed by a **hexagonal skirted underfloor seal** (108–114). Source post: https://x.com/SawyerMerritt/status/2111542537202651638. Original: https://pbs.twimg.com/media/HU20v1XXkAAGBob.png |
| `patent_fan_suction_fig_sawyermerritt_2.jpg` | Official-render crop of the rear: 4 gold-lined thruster bells above 4 fans (conical spinners, about 7 blades) in a carbon box with 3 strakes, plus the RDSTR plate. Original: https://pbs.twimg.com/media/HU20MuMWAAAA4Fp.jpg |

## C. UI and cabin frames (screen captures, in `images/ui_frames/`)
**Source 1:** Tesla's official UI video, https://x.com/Tesla/status/2113730362232680854 (2026-10-23). MP4 variants: `https://video.twimg.com/amplify_video/2113730214635208705/vid/avc1/{1920x1080/NqzDSGKDkyI63ajz | 3840x2160/n1R5zk4uTiuFuMI_ | 1280x720/YG8qhtXdzTL8-BxL}.mp4`

| File | Content |
|---|---|
| `ui_t01_intro_closeup_prnd.jpg` | Screen beside the yoke: PRND, 65%, drive strip, car with airflow |
| `ui_t04_home_rear_wing_menu.jpg` | Home: rear 3/4 x-ray, Rear Wing callout (Stowed / Low Drag / High Downforce), music strip, tabs |
| `ui_t06_home_side_barge_board.jpg` | Home: side wireframe, Barge Board callout |
| `ui_t08_home_front_hood_vent.jpg` | Home: front 3/4, Hood Vent callout, lit light bar |
| `ui_t10_home_exterior_lights.jpg` | Exterior Lights callout (Off / DRL / On / Auto) |
| `ui_t14_trackmode_dashboard_alt.jpg`, `ui_t20_trackmode_dashboard.jpg` | **Track Mode dashboard** (all tiles, thruster 5,500 PSI bar) |
| `ui_t16_trackmode_setup_auto.jpg`, `ui_t18_trackmode_setup_custom.jpg` | **Track Mode setup** (aero, fan, balance, ESC/TCS) |
| `ui_t22_vehicle_charging.jpg` | Vehicle > Charging (top-down x-ray) |
| `ui_t28_vehicle_dynamics_balance.jpg`, `ui_t32_vehicle_dynamics_custom.jpg` | Vehicle > Dynamics modes |
| `ui_t24_listen_home.jpg`, `ui_t26_listen_album.jpg` | Listen (media) screens |

**Source 2:** Sawyer Merritt in-car video, https://x.com/SawyerMerritt/status/2110937454580797625 (2026-10-16). MP4: `https://video.twimg.com/amplify_video/2110937183955603456/vid/avc1/1766x1080/SLDkqkzQDtXGatDl.mp4`

| File | Content |
|---|---|
| `cabin_pov_yoke_sawyermerritt.jpg` | Driver POV: closed squircle yoke with white rim, gray suede, pentagon hub |
| `cabin_touchscreen_bezel_boost_button_sawyermerritt.jpg` | Floating landscape screen with silver bezel, "BOOST" button on the yoke, carbon console |
| `cabin_pov_forward_display_sawyermerritt.jpg` | POV over the yoke; small red display near the windshield base on the right |

## D. Other videos (not downloaded; view online)
- Sawyer Merritt, 108 s in-depth UI (real car, includes the green charge bar and a climate "AUTO/HI" cluster): https://x.com/SawyerMerritt/status/2110949599351329267. MP4: https://video.twimg.com/amplify_video/2110948530894319616/vid/avc1/1920x1080/TGsS7o4rXzTZki6i.mp4
- Wing deploy: https://x.com/SawyerMerritt/status/2110931813052944524. MP4: https://video.twimg.com/amplify_video/2110931509402128384/vid/avc1/1920x1080/dqlhgm1r8zhI-t3g.mp4
- Thrusters firing: https://x.com/SawyerMerritt/status/2110908481087680614. MP4: https://video.twimg.com/amplify_video/2110908363940745216/vid/avc1/1920x1080/CIA1EOgL9-TZKnMa.mp4
- Reverse-camera hedgehog easter egg: https://x.com/SawyerMerritt/status/2110951012882084016
- Tire sizes (295/30R20 F, 335/30R21 R) observation video: https://x.com/CARN0N/status/2111094591038648483
- Top Gear 49-min interview with Moravy and von Holzhausen (2026-10-16): search "Top Gear Tesla Roadster Tom Ford".
- Hagerty explainer: https://www.hagerty.com/media/videos/the-flying-tesla-roadster-explained-by-the-man-who-engineered-it/

## E. Third-party galleries (likely re-hosted Tesla press shots)
- NetCarShow, labeled "2026 Roadster Concept": https://www.netcarshow.com/tesla/2026-roadster_concept/
  - Side_Profile: https://www.netcarshow.com/Tesla-Roadster_Concept-2026-Side_Profile.b11d6d0a.jpg
  - Front, Rear, Rear_Three-Quarter, Interior: same pattern
  - Wallpaper: https://www.netcarshow.com/Tesla-Roadster_Concept-2026-wallpaper.jpg
- Press galleries: InsideEVs https://insideevs.com/news/812151/2027-tesla-roadster-price-specs-release-date/ ; Motor1 headlights https://www.motor1.com/news/812660/tesla-roadster-pop-up-headlights/ ; CarBuzz https://carbuzz.com/new-tesla-roadster-debut-features-specs/

## F. Blueprint status
**No real blueprint or orthographic 3-view has been published.** For a blueprint set, use:
- **Side:** `02-Pilot` (near-orthographic; scale with wheelbase 2,825 mm, which is 1,261 px between the axle centers at x ≈ 1699 and x ≈ 2960)
- **Front:** `03-Slide-1`
- **Rear:** `01-Hero` or the lower half of `06-Gravity`
- **Plan:** `03-Slide-3` (has perspective; correct for it)

Expect ±3–5% error from lens perspective.
