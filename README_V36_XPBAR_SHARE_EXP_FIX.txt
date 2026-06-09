v36 XP bar fix based on your working v34 / BIG JIMMY DAWG build.

Fixes XP gain line:
  You receive your share of experience -- 175 points.

Behavior:
- If the XP bar already knows 'need to advance', it subtracts the gained XP and fills the bar.
- If the XP bar does not know the total yet, it stores the gain, shows +XP, and sends 'info' to sync.
- When the info line arrives, it uses current need + pending gained XP as the XP bar total so the bar is already filled by the amount earned.
