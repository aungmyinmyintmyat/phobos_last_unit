# Mission 3: collect USB, locate S-01 and save its scan

After Mission 2 repairs C-02, go to the Monitor Room PC at **(26.5,32.5)**. Press **E / COLLECT USB**. The terminal prepares a USB drive and downloads S-01's coordinates for four seconds. Stay nearby and grounded; leaving or jumping cancels preparation, and pause freezes it. Successful preparation adds exactly one **USB drive** to the inventory and reveals **S-01 at (119,60)**. The USB is distinct from the Mars Exploration chip in Mission 4.

Carry the USB across Map1 to S-01 through the cliff opening at x=114..118, y=53..54. Press **E** near S-01 to scan for **four seconds**, staying grounded within 2.5 units with line of sight. The scan cannot start without the USB. Leaving cancels the scan without creating a completed record. A successful scan saves the result onto the same USB and completes Mission 3.

Press **N** or Continue to begin Mission 4 at the same location. On the way back to base, discover and collect the **Mars Exploration data chip at (112,48)**. Both items remain in the inventory. Enter the base and use the Monitor Room PC to upload the USB scan record and scan the Mars chip, opening the four-passage, nine-question quiz. Completing the quiz unlocks Mission 6, followed by 7 and 8.

The campaign regression test checks both pickups, cancelled and completed scans, carrying both items along the return route, uploading at the PC, all nine quiz answers, and consuming the USB and chip on final quiz confirmation before continuing to Mission 6.

`horizon_mission1_tests` plays the campaign from supply recovery through observations and carrier repair to the PC, and verifies direct Mission 2 entry, checking automatic progression, interaction gating, exact timing, pausing, cancellation, retry, rendering and reset. Passing an output directory to the test also writes `records-start.ppm` and `records-copying.ppm` from actual gameplay.
