"""
TonConnect Example Level Setup
================================
Creates /TonConnect/Maps/TonConnectExample.umap with:
  - Sky atmosphere + directional sun + sky light
  - Floor plane
  - TonConnectDemoActor (mock mode, auto-connect on BeginPlay)
  - PlayerStart positioned for a good overview
  - TonConnectDemoGameMode as the world game mode

Run from UE Editor:
  File → Execute Python Script → <this file>
  OR in Output Log:
  py "Plugins/TonConnect/Scripts/setup_example_level.py"
"""

import unreal

# ── Config ────────────────────────────────────────────────────────────────────
LEVEL_PACKAGE  = "/TonConnect/Maps/TonConnectExample"
DEMO_CLASS_PATH = "/Script/TonConnect.TonConnectDemoActor"
GM_CLASS_PATH   = "/Script/TonConnect.TonConnectDemoGameMode"

# ── Helpers ───────────────────────────────────────────────────────────────────

def spawn(cls, location=(0,0,0), rotation=(0,0,0)):
    loc = unreal.Vector(*location)
    rot = unreal.Rotator(*rotation)
    return unreal.EditorLevelLibrary.spawn_actor_from_class(cls, loc, rot)

def log(msg):
    unreal.log("[TonConnect] " + msg)

# ── Create level ──────────────────────────────────────────────────────────────

log("Creating example level at " + LEVEL_PACKAGE)
unreal.EditorLevelLibrary.new_level(LEVEL_PACKAGE)

# ── Lighting ──────────────────────────────────────────────────────────────────

# Sky atmosphere
spawn(unreal.SkyAtmosphere)
log("Added SkyAtmosphere")

# Directional light (sun)
sun = spawn(unreal.DirectionalLight, location=(0, 0, 1000), rotation=(-45, 45, 0))
sun.set_actor_label("Sun")
sun_comp = sun.get_component_by_class(unreal.DirectionalLightComponent)
if sun_comp:
    sun_comp.set_editor_property("intensity", 10.0)
    try:
        sun_comp.set_editor_property("atmosphere_sun_light", True)
    except Exception:
        pass  # older engine versions may not expose this
log("Added DirectionalLight (sun)")

# Sky light
sky = spawn(unreal.SkyLight, location=(0, 0, 0))
sky.set_actor_label("SkyLight")
sky_comp = sky.get_component_by_class(unreal.SkyLightComponent)
if sky_comp:
    sky_comp.set_editor_property("source_type",
        unreal.SkyLightSourceType.SLS_CAPTURED_SCENE)
log("Added SkyLight")

# ── Floor ─────────────────────────────────────────────────────────────────────

floor_mesh = unreal.load_asset("/Engine/BasicShapes/Plane")
floor = spawn(unreal.StaticMeshActor, location=(0, 0, -100))
floor.set_actor_label("Floor")
floor.set_actor_scale3d(unreal.Vector(20, 20, 1))
floor_smc = floor.get_component_by_class(unreal.StaticMeshComponent)
if floor_smc and floor_mesh:
    floor_smc.set_static_mesh(floor_mesh)
log("Added floor plane")

# ── Demo actor ────────────────────────────────────────────────────────────────

demo_class = unreal.load_class(None, DEMO_CLASS_PATH)
if not demo_class:
    unreal.log_error("[TonConnect] Could not load TonConnectDemoActor class. "
                     "Make sure the plugin is compiled and enabled.")
else:
    demo = spawn(demo_class, location=(0, 0, 0))
    demo.set_actor_label("TonConnect_Demo")
    log("Spawned TonConnectDemoActor")

# ── Player start ──────────────────────────────────────────────────────────────

ps = spawn(unreal.PlayerStart, location=(0, -800, 200), rotation=(-10, 90, 0))
ps.set_actor_label("PlayerStart")
log("Added PlayerStart")

# ── Game mode ─────────────────────────────────────────────────────────────────

gm_class = unreal.load_class(None, GM_CLASS_PATH)
if not gm_class:
    unreal.log_warning("[TonConnect] Could not load TonConnectDemoGameMode. "
                       "Set it manually in World Settings → Game Mode.")
else:
    world = unreal.EditorLevelLibrary.get_editor_world()
    if world:
        ws = world.get_world_settings()
        ws.set_editor_property("default_game_mode", gm_class)
        log("Set game mode to TonConnectDemoGameMode")

# ── Save ──────────────────────────────────────────────────────────────────────

unreal.EditorLevelLibrary.save_current_level()
log("Level saved to " + LEVEL_PACKAGE)
log("Done! Press Play to test. Keys in-game: 1=Connect  2=Send  3=Disconnect  4=SendToSelf")
