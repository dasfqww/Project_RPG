# Combat Hit Query

## Goal

Provide one spatial targeting layer for player skills, common PvE attacks, and
boss patterns. The query layer discovers targets and returns geometry results;
it does not select AI actions, apply damage, or own persistent-area timing.

## Runtime flow

```text
Gameplay Ability or AI pattern executor
    -> Target selector
    -> FRPGHitQueryContext
    -> URPGHitQuerySubsystem
    -> FRPGHitQueryResult[]
    -> Gameplay Effect / gameplay event
```

The same `FRPGHitQueryProfile` must also drive telegraph rendering so the
displayed area and authoritative hit area do not drift apart.

## Ownership

- Gameplay Ability: cost, cooldown, prediction, animation, effect application.
- AI/StateTree: pattern choice, phase rules, telegraph/active/recovery timing.
- Hit Query: broad-phase collision query, shape math, target eligibility,
  ordering, result limiting, and optional line of sight.
- Persistent area: enter/exit/periodic policy and per-activation hit history.
- Gameplay Effect: damage, healing, crowd control, immunity, and attributes.

## Query stages

1. Use Unreal overlap or sweep to collect nearby collision candidates.
2. Deduplicate actors.
3. Apply team and Gameplay Tag filters.
4. Apply exact narrow-phase shape math.
5. Optionally test line of sight.
6. Sort and apply `MaxResults`.

Sector and ring-sector shapes are ground-plane shapes: local XY defines range
and angle, while `HalfHeight` defines vertical tolerance. Their angular test
uses a precomputed cosine comparison rather than `acos`.

## Authority

Authoritative damage queries run on the server. Clients may predict animation,
telegraphs, and local feedback, but should submit aim/origin intent rather than
an authoritative actor list.

## Initial implementation

- Shapes: sphere, capsule, box, sector, ring sector, line sweep.
- Filters: team, required/blocked Gameplay Tags, line of sight, ignored actors,
  per-activation already-hit actors, ordering, and maximum targets.
- Debug drawing and automation-tested shape math.

## Follow-up milestones

1. Build one authoritative 90-degree melee skill with the shared Ability Task.
2. Build one telegraphed boss ring-sector pattern with the same profile.
3. Add persistent-area enter/exit/periodic execution and pooling.
4. Profile large PvE encounters and batch or stagger queries where necessary.
