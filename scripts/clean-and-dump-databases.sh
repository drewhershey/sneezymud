#!/bin/bash
#
# Sanitize the database and export to _Setup-data/ as clean seed data.
#
# This is the heavy-duty version of dump-databases.sh. Use it after
# restoring a production backup or any database that contains accumulated
# player state, player-owned corporations, custom shop pricing, etc.
# It applies the full sanitization (removing player corps, resetting shop
# config, capitalizing corp banks for economy bootstrapping), then dumps
# everything and cleans up stale files in _Setup-data/.
#
# The database is backed up before sanitization and restored afterward
# (or on failure/interrupt), so the original data is never lost.
#
# For routine re-dumps against an already-clean database, use
# dump-databases.sh instead - it just dumps without modifying anything.
#
# Output is deterministic - timestamps, version headers, and runtime
# AUTO_INCREMENT counters are stripped so that git diffs reflect only real
# schema or data changes. The MariaDB server version is not recorded; the
# dump format's /*!NNNNN ...*/ conditional comments handle cross-version
# compatibility natively.
#
# Usage:
#   scripts/clean-and-dump-databases.sh              # sanitize + dump
#   scripts/clean-and-dump-databases.sh --dry-run    # show what would happen

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SETUP_DIR="$(cd "$SCRIPT_DIR/../_Setup-data" && pwd)"

DRY_RUN=false
[[ "${1:-}" == "--dry-run" ]] && DRY_RUN=true

PLAYER_CORP_IDS="2,3,4,6,7,8,11,12,13,17,24,30"

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

# --- Helpers ---

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

run_sql() {
  local db="$1"
  shift
  if $DRY_RUN; then
    echo "  would run on $db: $*"
  else
    mariadb "$db" -e "$*"
  fi
}

# --- Step 0: Back up databases before sanitizing ---

BACKUP_DIR=$(mktemp -d)
trap 'restore_backup' EXIT

backup_databases() {
  if $DRY_RUN; then return; fi
  echo "=== Backing up databases ==="
  mariadb-dump --single-transaction sneezy > "$BACKUP_DIR/sneezy.sql"
  mariadb-dump --single-transaction immortal > "$BACKUP_DIR/immortal.sql"
  echo "  backed up to $BACKUP_DIR"
}

restore_backup() {
  if $DRY_RUN; then rm -rf "$BACKUP_DIR"; return; fi
  echo "=== Restoring databases ==="
  mariadb sneezy < "$BACKUP_DIR/sneezy.sql"
  mariadb immortal < "$BACKUP_DIR/immortal.sql"
  rm -rf "$BACKUP_DIR"
  echo "  databases restored to pre-sanitization state"
}

backup_databases

# --- Step 1: Reassign player-corp shops to game-owned corps ---
# Must happen before deleting corps due to FK on shopowned.corp_id.

echo "=== Sanitizing database ==="

echo "Reassigning player-corp shops..."
# To Grimhaven (corp 21): Upper Class Area, King's Palace, GK Casino,
# Grimhaven Outer Pathway, Icy Wastelands
run_sql sneezy "UPDATE shopowned SET corp_id = 21
  WHERE shop_nr IN (113,114,115,117,118,119,120,121,122,135,137,139,143,
                    159,172,173,227,228,229,230);"

# To SBA (corp 1): Bullywug Swamp, Sleeptag Zone, Glodson's Mining Camp
run_sql sneezy "UPDATE shopowned SET corp_id = 1
  WHERE shop_nr IN (166,183,255,256,257,258);"

# To Realm of Cult of Logrus (corp 28): Damescena areas
run_sql sneezy "UPDATE shopowned SET corp_id = 28
  WHERE shop_nr IN (13, 178, 181);"

# --- Step 2: Remove player-owned corporations ---

echo "Removing player-owned corporations..."
run_sql sneezy "DELETE FROM corpaccess WHERE corp_id IN ($PLAYER_CORP_IDS);"
run_sql sneezy "DELETE FROM corplog WHERE corp_id IN ($PLAYER_CORP_IDS);"
run_sql sneezy "DELETE FROM shopownedcorpbank WHERE corp_id IN ($PLAYER_CORP_IDS);"
run_sql sneezy "DELETE FROM corporation WHERE corp_id IN ($PLAYER_CORP_IDS);"

# --- Step 3: Sanitize shopowned ---

echo "Sanitizing shop config..."
run_sql sneezy "UPDATE shopowned SET
  gold = 1000000,
  profit_buy = 1.1,
  profit_sell = 0.9,
  reserve_min = 900000,
  reserve_max = 1100000,
  dividend = NULL;"

# --- Step 4: Clear player-configured shop overrides ---

echo "Clearing player shop overrides..."
run_sql sneezy "DELETE FROM shopownedratios;"
run_sql sneezy "DELETE FROM shopownedmatch;"
run_sql sneezy "DELETE FROM shopownedplayer;"

# --- Step 5: Zero corporation bank balances ---
# Shops are seeded with gold at the reserve midpoint (step 3). Corps
# accumulate bank balances naturally as shops deposit excess above
# reserve_max via doReserve().

echo "Zeroing corp bank balances..."
run_sql sneezy "UPDATE shopownedcorpbank SET talens = 0, earned_interest = 0;"

# Ensure every corp has a bank entry at its configured bank shop.
# Production backups may be missing entries for corps that never deposited.
run_sql sneezy "INSERT IGNORE INTO shopownedcorpbank (shop_nr, corp_id, talens, earned_interest)
  SELECT c.bank, c.corp_id, 0, 0 FROM corporation c
  LEFT JOIN shopownedcorpbank cb ON c.corp_id = cb.corp_id AND c.bank = cb.shop_nr
  WHERE cb.corp_id IS NULL;"

# --- Step 6: Clear accumulated game state ---

echo "Clearing accumulated game state..."
run_sql sneezy "DELETE FROM rent_obj_aff;"
run_sql sneezy "DELETE FROM rent_strung;"
run_sql sneezy "DELETE FROM rent;"
run_sql sneezy "DELETE FROM permadeath;"

# --- Step 7: Verify ---

echo "=== Verifying ==="

if ! $DRY_RUN; then
  orphaned=$(mariadb sneezy -N -e "SELECT COUNT(*) FROM shopowned s
    LEFT JOIN corporation c ON s.corp_id = c.corp_id WHERE c.corp_id IS NULL;")
  missing_bank=$(mariadb sneezy -N -e "SELECT COUNT(DISTINCT s.corp_id) FROM shopowned s
    LEFT JOIN shopownedcorpbank cb ON s.corp_id = cb.corp_id WHERE cb.corp_id IS NULL;")
  no_reserves=$(mariadb sneezy -N -e "SELECT COUNT(*) FROM shopowned
    WHERE reserve_min = 0 OR reserve_max = 0;")
  player_corps=$(mariadb sneezy -N -e "SELECT COUNT(*) FROM corporation
    WHERE corp_id IN ($PLAYER_CORP_IDS);")
  remaining_rent=$(mariadb sneezy -N -e "SELECT COUNT(*) FROM rent;")

  fail=false
  [[ "$orphaned" -ne 0 ]] && echo "FAIL: $orphaned orphaned shops" && fail=true
  [[ "$missing_bank" -ne 0 ]] && echo "FAIL: $missing_bank corps missing bank entries" && fail=true
  [[ "$no_reserves" -ne 0 ]] && echo "FAIL: $no_reserves shops without reserves" && fail=true
  [[ "$player_corps" -ne 0 ]] && echo "FAIL: $player_corps player corps remain" && fail=true
  [[ "$remaining_rent" -ne 0 ]] && echo "FAIL: $remaining_rent rent rows remain" && fail=true

  if $fail; then
    echo "Verification failed, aborting before dump."
    exit 1
  fi
  echo "All checks passed."
fi

# --- Step 8: Dump ---

echo "=== Dumping ==="

declare -A is_seed_table
for t in "${SNEEZY_SEED_TABLES[@]}"; do
  is_seed_table[$t]=1
done

# Sneezy database
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
    # Remove stale data file if it exists
    if [[ -f "$SETUP_DIR/sql_data/sneezy/$table.sql" ]]; then
      if $DRY_RUN; then
        echo "  would remove stale: sql_data/sneezy/$table.sql"
      else
        rm "$SETUP_DIR/sql_data/sneezy/$table.sql"
        echo "  removed stale: sql_data/sneezy/$table.sql"
      fi
    fi
    schema_count=$((schema_count + 1))
  fi
done

echo "  $seed_count with seed data, $schema_count schema-only"

# Remove sql_tables/sneezy files for tables that no longer exist
for f in "$SETUP_DIR/sql_tables/sneezy/"*.sql; do
  [[ -f "$f" ]] || continue
  table="$(basename "$f" .sql)"
  found=false
  for t in "${sneezy_tables[@]}"; do
    [[ "$t" == "$table" ]] && { found=true; break; }
  done
  if ! $found; then
    if $DRY_RUN; then
      echo "  would remove dropped: sql_tables/sneezy/$table.sql"
    else
      rm "$f"
      echo "  removed dropped: sql_tables/sneezy/$table.sql"
    fi
  fi
done

# Remove sql_data/sneezy files for tables that no longer exist
for f in "$SETUP_DIR/sql_data/sneezy/"*.sql; do
  [[ -f "$f" ]] || continue
  table="$(basename "$f" .sql)"
  found=false
  for t in "${sneezy_tables[@]}"; do
    [[ "$t" == "$table" ]] && { found=true; break; }
  done
  if ! $found; then
    if $DRY_RUN; then
      echo "  would remove dropped: sql_data/sneezy/$table.sql"
    else
      rm "$f"
      echo "  removed dropped: sql_data/sneezy/$table.sql"
    fi
  fi
done

# Immortal database (schema-only, builder staging area)
mapfile -t immortal_tables < <(mariadb -N -e "SHOW TABLES" immortal)
echo "immortal: ${#immortal_tables[@]} tables (all schema-only)"

for table in "${immortal_tables[@]}"; do
  dump_one immortal "$table" schema "$SETUP_DIR/sql_tables/immortal/$table.sql"

  # Remove stale data files
  if [[ -f "$SETUP_DIR/sql_data/immortal/$table.sql" ]]; then
    if $DRY_RUN; then
      echo "  would remove stale: sql_data/immortal/$table.sql"
    else
      rm "$SETUP_DIR/sql_data/immortal/$table.sql"
      echo "  removed stale: sql_data/immortal/$table.sql"
    fi
  fi
done

# Remove sql_tables/immortal files for dropped tables
for f in "$SETUP_DIR/sql_tables/immortal/"*.sql; do
  [[ -f "$f" ]] || continue
  table="$(basename "$f" .sql)"
  found=false
  for t in "${immortal_tables[@]}"; do
    [[ "$t" == "$table" ]] && { found=true; break; }
  done
  if ! $found; then
    if $DRY_RUN; then
      echo "  would remove dropped: sql_tables/immortal/$table.sql"
    else
      rm "$f"
      echo "  removed dropped: sql_tables/immortal/$table.sql"
    fi
  fi
done

# Remove old SQL migration scripts if present
if [[ -d "$SETUP_DIR/migrations" ]]; then
  if $DRY_RUN; then
    echo "  would remove: _Setup-data/migrations/"
  else
    rm -rf "$SETUP_DIR/migrations"
    echo "  removed: _Setup-data/migrations/"
  fi
fi

echo "done."
