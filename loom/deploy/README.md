# loom/deploy przeniesiony

Wdrożenie (Docker, systemd, Caddy, paczka, rollback) jest w katalogu głównym: [`deploy/`](../../deploy/README.md).
`deploy/install.sh` zastąpił `ec2-setup.sh`, a `deploy/Dockerfile` + `deploy/docker-compose.yml` zastąpiły
tutejsze pliki. Skrypty pomocnicze `build-linux.sh` i `build-android.sh` leżą teraz w `deploy/`.
