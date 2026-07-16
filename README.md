# ir-remote

ESPHome infrared remote (ESP8266) with a local `universal_ac_climate` component and offline IRext catalog tooling.

## Layout

```text
configs/                     ESPHome device configs
  ir3.yaml                   Main device config
  ir3_2m.yaml                4MB flash / OTA partition overlay
components/                  Local ESPHome external components
  universal_ac_climate/      Climate entity + UniversalAc logic
data/irext/                  Raw IRext assets used by the exporter
  bin/                       Protocol binary pack
  db/                        SQLite index
generated/irext/             Exported C++ catalog headers
partitions/                  Custom ESP8266 linker scripts
tools/                       Offline tooling
vendor/irext_core/           Upstream IRext decode core (not yet wired)
```

## Build / flash

From the repo root:

```bash
esphome run configs/ir3.yaml
# 4MB board with larger OTA slots (serial flash first time):
esphome run configs/ir3_2m.yaml
```

## Regenerate IRext catalog headers

```bash
python tools/export_irext_catalog.py data/irext/db/irext_db_20260519_sqlite3.db \
  --binary-dir data/irext/bin \
  --output-dir generated/irext \
  --emit-header
```
