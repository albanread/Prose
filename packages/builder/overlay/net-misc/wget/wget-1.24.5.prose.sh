# wget 1.24.5: the recipe downloads from mirror.easyname.at, whose TLS
# certificate has expired (curl: certificate has expired, 2026-09-19). The
# GNU server has the same file; the recipe's checksum still applies.
SOURCE_URI="https://ftp.gnu.org/gnu/wget/wget-$portVersion.tar.gz"
