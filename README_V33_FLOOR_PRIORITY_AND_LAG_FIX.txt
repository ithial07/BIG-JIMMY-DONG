v33 changes

1. Lag reduction: image icons are now pixmap-cached instead of being reloaded every paint.
2. [ Floor ] always wins first and always uses decorative_cobblestone_tile_icon.png.
3. Removed special handling based on room names containing cave/tunnel/mine/underground.
   That means cave-named floor rooms now stay on the chosen floor tile.
4. Kept the text-size option from v32.
