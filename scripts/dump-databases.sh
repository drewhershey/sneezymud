#!/bin/bash
#
# Export the sneezy and immortal databases to _Setup-data/ as SQL dump files,
# used by the Docker database image (sneezymud-docker) to initialize fresh
# game server instances.
#
# Two kinds of dump are produced per table:
#   sql_tables/  - Schema only (CREATE TABLE). Every table gets one.
#   sql_data/    - Schema + row data (CREATE TABLE + INSERT). Only tables
#                  listed in SNEEZY_SEED_TABLES get one. These contain the
#                  static world definitions, economy config, and migration
#                  version needed to boot a playable server from scratch.
#                  The immortal database is entirely schema-only (it's a
#                  builder staging area for in-progress edits).
#
# Output is deterministic - timestamps, version headers, and runtime
# AUTO_INCREMENT counters are stripped so that git diffs reflect only real
# schema or data changes. The MariaDB server version is not recorded; the
# dump format's /*!NNNNN ...*/ conditional comments handle cross-version
# compatibility natively.
#
# Run this:
#   - To capture changes to table schema or seed data after adding and running new migrations
#   - To capture updates made to world data by builders (rooms, mobs, objects, shops, etc.)
#
# Usage:
#   scripts/dump-databases.sh              # dump from local database
#   scripts/dump-databases.sh --dry-run    # show what would be dumped

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SETUP_DIR="$(cd "$SCRIPT_DIR/../_Setup-data" && pwd)"

DRY_RUN=false
[[ "${1:-}" == "--dry-run" ]] && DRY_RUN=true

# Update this list when adding new tables that need seed data.
SNEEZY_SEED_TABLES=(
  # World definitions
  mob mob_extra mob_imm mobresponses
  obj objaffect objextra
  room roomexit roomextra
  zone
  shop shoptype shopmaterial shopproducing
  ship_master ship_destinations
  globaltoggles
  property

  # Economy infrastructure
  shopowned
  corporation
  shopownedcorpbank
  shoplogaccountchart
  shopownedrepair

  # Migration tracking
  configuration

  # Factory system (vestigial but valid config)
  factoryblueprint factoryproducing factorysupplies
)

# Strip table-level AUTO_INCREMENT=N (runtime counter, not schema).
# Column-level AUTO_INCREMENT keyword is unaffected (no '=' after it).
clean_dump() {
  sed 's/ AUTO_INCREMENT=[0-9]*//'
}

dump_one() {
  local db="$1" table="$2" mode="$3" outfile="$4"
  local flags=(--skip-comments --skip-dump-date)
  [[ "$mode" == "schema" ]] && flags+=(--no-data)

  if $DRY_RUN; then
    echo "  would dump: $db.$table ($mode) -> $(basename "$outfile")"
    return
  fi

  mariadb-dump "${flags[@]}" "$db" "$table" | clean_dump > "$outfile"
}

declare -A is_seed_table
for t in "${SNEEZY_SEED_TABLES[@]}"; do
  is_seed_table[$t]=1
done

mapfile -t sneezy_tables < <(mariadb -N -e "SHOW TABLES" sneezy)
echo "sneezy: ${#sneezy_tables[@]} tables"

seed_count=0
schema_count=0

for table in "${sneezy_tables[@]}"; do
  dump_one sneezy "$table" schema "$SETUP_DIR/sql_tables/sneezy/$table.sql"

  if [[ -n "${is_seed_table[$table]:-}" ]]; then
    dump_one sneezy "$table" data "$SETUP_DIR/sql_data/sneezy/$table.sql"
    seed_count=$((seed_count + 1))
  else
    schema_count=$((schema_count + 1))
  fi
done

echo "  $seed_count with seed data, $schema_count schema-only"

mapfile -t immortal_tables < <(mariadb -N -e "SHOW TABLES" immortal)
echo "immortal: ${#immortal_tables[@]} tables (all schema-only)"

for table in "${immortal_tables[@]}"; do
  dump_one immortal "$table" schema "$SETUP_DIR/sql_tables/immortal/$table.sql"
done

echo "done."
