# Captures

Raw data recorded from the stock board during Phase 1. Voltages are real values (scaled back up through the 47k/22k dividers).

| File | Source sketch | What happened |
| --- | --- | --- |
| `right_arm_run1.csv` | freddy_logger (10 analog columns) | First full right-arm show. Columns: `t_ms, r1..r10` (no header, no marker) |
| `right_arm_run2_left_connected.csv` | freddy_logger (with marker) | Right arm; marker held while the right elbow moved. Left arm connected |
| `right_arm_run3_elbow_marked.csv` | freddy_logger (with marker) | Same, left arm unplugged. Row 5 shows the elbow's serial bursts |
| `right_elbow_uart_9600.txt` | PulseView UART export | Decoded elbow frames at 9600 baud (bits and bytes) |
| `head_show.csv` | freddy_head_monitor | Full show: eye motor, home switch, LEDs, supply |
| `left_shoulder.csv` | freddy_head_monitor | Left shoulder motor on GPIO1/2 (columns `h1y`/`h1w`; `d1` = voltage across the motor) |

Logger columns: `t_ms, r1..r9, mark` (rows of the [tap board map](../docs/hardware-map.md#tap-board-row-map-arm)).

Head monitor columns: `t_ms, h1y, h1w, h2y, h2w, h3y, h3o, vin, d1, d2, d3, *_rip (x6), mark`.
