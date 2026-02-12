export LIBRARY_PATH=/var/tmp/rust/release:$LIBRARY_PATH
../configure --target-list=aarch64-softmmu,arm-softmmu,loongarch64-softmmu,riscv32-softmmu,riscv64-softmmu,x86_64-softmmu \
--without-default-features --enable-capstone --enable-plugins --enable-curses --enable-slirp --enable-iconv \
--enable-linux-aio --enable-linux-io-uring --enable-bpf --enable-wa2x

# --enable-rust