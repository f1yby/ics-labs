lab6 debian/ubuntu missing tk8.5-dev tcl8.5-dev general solution
```bash
wget http://ftp.sjtu.edu.cn/ubuntu/pool/universe/t/tcl8.5/libtcl8.5_8.5.19-4_amd64.deb http://ftp.sjtu.edu.cn/ubuntu/pool/universe/t/tcl8.5/tcl8.5_8.5.19-4_amd64.deb http://ftp.sjtu.edu.cn/ubuntu/pool/universe/t/tcl8.5/tcl8.5-dev_8.5.19-4_amd64.deb http://ftp.sjtu.edu.cn/ubuntu/pool/universe/t/tk8.5/libtk8.5_8.5.19-3_amd64.deb http://ftp.sjtu.edu.cn/ubuntu/pool/universe/t/tk8.5/tk8.5_8.5.19-3_amd64.deb http://ftp.sjtu.edu.cn/ubuntu/pool/universe/t/tk8.5/tk8.5-dev_8.5.19-3_amd64.deb
sudo apt install ./libtcl8.5_8.5.19-4_amd64.deb ./tcl8.5_8.5.19-4_amd64.deb ./tcl8.5-dev_8.5.19-4_amd64.deb ./libtk8.5_8.5.19-3_amd64.deb ./tk8.5_8.5.19-3_amd64.deb ./tk8.5-dev_8.5.19-3_amd64.deb
```
