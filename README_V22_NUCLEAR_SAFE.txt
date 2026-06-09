V23 NUCLEAR SAFE BUILD

This build is made to stop the repeated startup crash loop.

It includes:
- Full client based on last stable background/full-room-id version.
- Portable profile stored beside the EXE, so old broken AppData settings cannot crash it.
- Startup log: package-windows\ardabest_startup_log.txt
- Safe fallback EXE: ardabest_client_safe.exe
- Launcher: RUN-ARDABEST-CLIENT.bat tries full client, then automatically opens safe fallback if full crashes.
- RUN-SAFE-MODE-ONLY.bat opens the fallback directly.
- RESET-FULL-PROFILE-AND-RUN.bat backs up the portable profile and starts fresh.
- Text color dropdown added to the full client.

Use DOUBLE-CLICK-ME-WINDOWS.bat from the source folder.
Then run package-windows\RUN-ARDABEST-CLIENT.bat.
