# Samples

- `tutorial/` - the language step by step. Numbered (`01_first_grid.lvs`, ...), read them in order; see `docs/tutorial-plan.md`.
- `techniques/` - self-contained generation techniques (cellular automata, walks, ...), not tied to a game.
- `games/` - generators that follow the level generation of a real game. A game made of several modules gets its own subfolder.
- `tiles/` - tilesets the samples' `.lvs.json` display settings refer to (as `../tiles/...`, or `../../tiles/...` from a subfolder).

Every `.lvs` here also shows up live on the website's samples page (`tools/build_site_data.py`).
Names must be unique across folders: the page links to each sample by its bare name.
