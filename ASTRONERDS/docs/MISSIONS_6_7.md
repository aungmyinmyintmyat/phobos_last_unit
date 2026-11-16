# S-01 rescue sequence

Mission 3 ends after collecting the USB and recording a scan of **S-01 at (119,60)**
for four seconds using **E**. Continue to Mission 4 to recover the Mars data chip
at **(112,48)** and verify all nine answers at the Monitor Room PC. Press **N**
or choose **Continue / Mission 6** after the quiz. Gameplay continues inside the
same ship with the existing inventory and map state. There is no Mission 5 entry.
Mission 6 can also be selected directly from the Missions menu.

In **Mission 6**, walk to the Storage Room and press **E** near each green supply
marker to gather hull plates, a wiring kit and sealant. The supplies remain in
U-01's inventory for later spaceship repairs. The last pickup starts Mission 7.

In **Mission 7**, go to the Cargo Hangar and press **E** near **C-02** to communicate
about transporting the injured Astronerd. An automatic sequence shows the
conversation, seat installation, both robots walking to the bed, and the
Astronerd being lifted onto C-02 and secured with a harness. Pause freezes the
sequence; movement controls resume after a fade to the exterior of the Map1 base.

Control U-01 normally. C-02 carries the Astronerd and follows automatically with
smooth acceleration, braking and turns. Its body, seat and passenger turn together,
and the wheels roll with travel and stop when parked. In clear terrain it steers
directly toward U-01; near obstacles it looks ahead along a safe route through the
cliff opening. Bring both robots to
S-01 at **(119,60)** to complete Mission 7 and begin Mission 8. C-02 is marked in amber on the minimap.

In **Mission 8**, press **E** near S-01 and stay nearby for six seconds to repair
the spacecraft using the four hull plates, wiring kit (1 kg of alloys) and
sealant (500 g of polyethylene) gathered in Mission 6. Leaving cancels the repair
without spending supplies; pausing freezes progress. Successful repairs consume
the supplies once and restore S-01's hull and lights. C-02 then moves to the
boarding ramp by routing around the hull and wings, with clearance for its whole
body. It stops outside the rear hatch and transfers the Astronerd along the ramp
through the open doorway into a seat below the cabin roof. The hatch closes
before charging begins. C-02 then transfers its entire
battery, bringing S-01 to 45% charge, and powers off. U-01 supplies the remaining
55% using all of its remaining battery and also powers off. Animated cables,
battery meters and a draining screen effect show the transfers. S-01 ignites its
engines and leaves with the Astronerd aboard; **both powerless robots remain on
the planet**. The shutdown is the successful ending, rather than a battery failure.
Pause freezes charging and launch, including after U-01 has powered off.

`make test` includes the rescue sequence regression test. `make preview-rescue`
runs that sequence and writes PPM frames of storage, communication, seat
installation, pickup, escort and arrival into `build/rescue-previews`.
