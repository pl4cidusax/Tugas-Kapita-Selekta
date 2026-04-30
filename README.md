# Mini Chat Client-Server (TCP Socket Python)

## Deskripsi
Program ini merupakan implementasi sistem client-server sederhana menggunakan protokol TCP di Python.
Server menerima perintah dari client untuk menyimpan pesan dan menampilkan riwayat pesan.

## File
- server.py  : Program server
- client.py  : Program client
- README.md  : Dokumentasi singkat penggunaan

## Cara Menjalankan

### 1. Jalankan Server
Buka terminal dan jalankan:
python server.py

Atau jika ingin menentukan port:
python server.py --port 5019

Server akan berjalan dan menunggu koneksi.

### 2. Jalankan Client
Buka terminal baru dan jalankan:
python client.py

Atau jika ingin menentukan host/port:
python client.py --host 127.0.0.1 --port 5019 --user nama_kamu

## Perintah di Client
- Ketik pesan biasa → akan dikirim ke server
- /history        → menampilkan 10 pesan terakhir
- /history 5      → menampilkan 5 pesan terakhir
- /help           → menampilkan bantuan
- /quit           → keluar dari client

## Catatan
- Server menggunakan TCP (SOCK_STREAM).
- Pesan disimpan di memori (akan hilang jika server restart).
- Log aktivitas disimpan di file chat.log.
