# Concepts and acceptance criteria

What each prototype must let a participant see or do, without being told.
Checked on the sandbox; the headless previews only show that the
mechanism exists.

## Prototype 4 ("Scene", branch `prototype/4-scene`)

### Step 1: lit, animated scene through a stylise filter

A participant can see, without being told:

1. the shape of the land through light and shade;
2. that the surface is alive and moving (grass in wind, light drifting)
   even when nobody touches it;
3. where the water is;
4. that touching the water or the sand beside it sends ripples across it.

### Next steps (not built yet)

- Step 2: grass density from the succession model's cover (replaces
  `grassDensity()` in `bin/data/shaders/scene/GL*/sceneRender.frag`).
- Step 3: Lenia organisms, a GPU continuous cellular automaton confined to
  the water by a habitat mask from the water simulation, drawn into the
  scene so the stylise pass treats them like everything else.
