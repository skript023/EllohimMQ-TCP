FROM ubuntu:22.04

# Install dependencies
RUN apt-get update && apt-get install -y \
    curl \
    ffmpeg \
    wget \
    nano \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Buat direktori kerja
WORKDIR /home/skript023/gbt-backend/Linux

# Copy binary dan konfigurasi
COPY Worker .
COPY .env .

# Install yt-dlp
RUN wget -q https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_linux -O /usr/local/bin/yt-dlp && \
    chmod a+rx /usr/local/bin/yt-dlp

# Set permission binary utama
RUN chmod +x ./Worker

# Pastikan folder output json tersedia
RUN mkdir -p /home/skript023/gbt-backend/Linux/json

# Tambahkan yt-dlp ke PATH (optional karena sudah ke /usr/local/bin)
ENV PATH="/usr/local/bin:$PATH"

# Jalankan aplikasi
CMD ["./Worker"]