"""Repository-local entrypoint; deliberately does not require installation."""
from retool.cli import main

if __name__ == "__main__":
    raise SystemExit(main())
