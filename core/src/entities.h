// Shared, lazy Player/Victim lifecycle observations. Only this service owns
// these game functions; subscribers run on the game thread, after the original
// callback (except destruction, which is observed before memory is freed).
#pragma once

namespace fdcore::entities {
using EntityFn = void (*)(void* entity);
bool ObservePlayer(EntityFn initialized, EntityFn updated);
bool ObserveVictims(EntityFn spawned, EntityFn updated, EntityFn destroying);
} // namespace fdcore::entities
