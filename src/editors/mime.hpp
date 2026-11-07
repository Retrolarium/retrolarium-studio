#pragma once

// Drag-and-drop formats shared by the editors and the inspector library.
namespace rl::editors {

// Payload: UTF-8 id of an inspector library item.
inline constexpr char kLibraryItemMime[] = "application/x-retrolarium-library-item";

// A game-specific JSON object, copied between compatible map models.
inline constexpr char kMapObjectMime[] = "application/x-retrolarium-map-object";

}  // namespace rl::editors
