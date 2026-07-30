import os
import unreal


ASSET_PATH = "/LocalMultimodalLLM/Examples/DA_ExampleLocalLLMToolSet"


def parameter(name, description, value_type, required=True, allowed=None):
    result = unreal.LocalLLMToolParameter()
    result.set_editor_property("name", name)
    result.set_editor_property("description", description)
    result.set_editor_property("type", value_type)
    result.set_editor_property("required", required)
    result.set_editor_property("allowed_values", allowed or [])
    return result


def tool(name, description, parameters, confirmation=False):
    result = unreal.LocalLLMToolDefinition()
    result.set_editor_property("name", name)
    result.set_editor_property("description", description)
    result.set_editor_property("parameters", parameters)
    result.set_editor_property("requires_player_confirmation", confirmation)
    return result


tools = [
    tool(
        "MoveToTarget",
        "Request that this character walk to a nearby target currently advertised by the game as reachable and safe. Unreal must resolve the target ID, recheck permission and navigation, choose speed and stopping distance, and report the actual outcome.",
        [parameter("target_id", "Stable ID from the game-provided actionable target list", unreal.LocalLLMToolValueType.STRING)],
    ),
    tool(
        "FaceTarget",
        "Request that this character turn to face a nearby target currently advertised by the game. Unreal must resolve the target ID, recheck permission, and control rotation behavior.",
        [parameter("target_id", "Stable ID from the game-provided actionable target list", unreal.LocalLLMToolValueType.STRING)],
    ),
    tool(
        "PlayGesture",
        "Request one short, non-gameplay gesture from the allow-list. The gesture communicates intent only and must never itself change inventory, quests, relationships, damage, or other authoritative state.",
        [
            parameter(
                "gesture",
                "Allow-listed cosmetic gesture",
                unreal.LocalLLMToolValueType.STRING,
                True,
                ["Nod", "ShakeHead", "Wave", "Point", "Shrug"],
            ),
        ],
    ),
]

asset_file = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "Content", "Examples", "DA_ExampleLocalLLMToolSet.uasset")
)
asset = unreal.load_asset(ASSET_PATH) if os.path.exists(asset_file) else None
if not asset:
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.LocalLLMToolSet)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "DA_ExampleLocalLLMToolSet",
        "/LocalMultimodalLLM/Examples",
        unreal.LocalLLMToolSet,
        factory,
    )

if not asset:
    raise RuntimeError("Could not create the Local LLM example Tool Set asset")

asset.set_editor_property("tools", tools)
unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)
unreal.log("Created or updated " + ASSET_PATH)
