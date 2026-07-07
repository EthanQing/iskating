from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path
from urllib.parse import urlsplit, urlunsplit

from sqlalchemy import create_engine

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from server.app.schema import create_schema, drop_schema  # noqa: E402


def database_url() -> str:
    value = os.getenv("ISKATING_DATABASE_URL", "").strip()
    if not value:
        raise RuntimeError("ISKATING_DATABASE_URL is required")
    return value


def masked_url(url: str) -> str:
    parts = urlsplit(url)
    netloc = parts.netloc
    if "@" in netloc:
        credentials, host = netloc.rsplit("@", 1)
        user = credentials.split(":", 1)[0]
        netloc = f"{user}:***@{host}"
    return urlunsplit((parts.scheme, netloc, parts.path, parts.query, parts.fragment))


def main() -> int:
    parser = argparse.ArgumentParser(description="Drop and recreate the current development PostgreSQL schema.")
    parser.add_argument("--yes", action="store_true", help="Required to confirm destructive schema reset.")
    parser.add_argument("--no-seed", action="store_true", help="Skip default seed data after recreating tables.")
    args = parser.parse_args()

    url = database_url()
    print(f"Target database: {masked_url(url)}")
    if not args.yes:
        print("Refusing to reset schema without --yes.")
        return 2

    engine = create_engine(url, pool_pre_ping=True)
    drop_schema(engine)
    create_schema(engine)
    if not args.no_seed:
        os.environ.setdefault("ISKATING_JWT_SECRET", "change-me-before-production")
        from server.app.main import seed_defaults  # noqa: WPS433

        seed_defaults()
    print("PostgreSQL schema reset complete.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
