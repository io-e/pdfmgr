#!/usr/bin/env bash
# Sets up a CUPS-PDF print queue for the "print" integration test (Debian/Ubuntu).
#   sudo apt install cups printer-driver-cups-pdf
#   scripts/setup-cups-pdf.sh            # creates queue "PDF", output in ~/PDF
# Then: PDFMGR_TEST_PRINTER=PDF PDFMGR_TEST_PRINT_OUT=$HOME/PDF ctest -L print
set -euo pipefail
QUEUE="${1:-PDF}"
# Start the scheduler if it is not running (containers/CI have no systemd unit started).
if ! lpstat -r >/dev/null 2>&1; then
  sudo systemctl start cups 2>/dev/null || sudo service cups start 2>/dev/null || sudo cupsd
  for _ in $(seq 1 50); do lpstat -r >/dev/null 2>&1 && break; sleep 0.2; done
fi
if ! lpstat -p "$QUEUE" >/dev/null 2>&1; then
  PPD=$(ls /usr/share/ppd/cups-pdf/CUPS-PDF_opt.ppd /usr/share/ppd/cups-pdf/*.ppd 2>/dev/null | head -1 || true)
  if [ -n "$PPD" ]; then sudo lpadmin -p "$QUEUE" -v cups-pdf:/ -E -P "$PPD"
  else sudo lpadmin -p "$QUEUE" -v cups-pdf:/ -E -m lsb/usr/cups-pdf/CUPS-PDF_opt.ppd; fi
fi
sudo cupsenable "$QUEUE"; sudo cupsaccept "$QUEUE"
mkdir -p "$HOME/PDF"
lpstat -p "$QUEUE" -v "$QUEUE"
echo "PDFMGR_TEST_PRINTER=$QUEUE PDFMGR_TEST_PRINT_OUT=$HOME/PDF"
