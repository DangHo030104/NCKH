void NhanDuLieu() {
  if (e32ttl.available()  > 1) {
    ResponseContainer rs = e32ttl.receiveMessage();
    // First of all get the data
    String message = rs.data;
    xulydulieu(message);

    if (t1 == 1) {
      nhietdo1 = t2 / 10.0;
      doam1    = t3 / 10.0;
      bui1     = t4 / 10.0;
      dienap1  = t5 / 100.0;
    }
    else if (t1 == 2) {
      nhietdo2 = t2 / 10.0;
      doam2    = t3 / 10.0;
      bui2     = t4 / 10.0;
      dienap2  = t5 / 100.0;
    }
  }
}

void xulydulieu(String mes) {
  str = (String)mes;
  s = str.indexOf("b") - str.indexOf("a");
  if (s == 2) {
    t1 = str[str.indexOf("a") + 1] - '0';
  }
  else if (s == 3) {
    chuc = str[str.indexOf("a") + 1] - '0';
    dv = str[str.indexOf("a") + 2] - '0';
    t1 = chuc * 10 + dv;
  }
  else if (s == 4) {
    tram = str[str.indexOf("a") + 1] - '0';
    chuc = str[str.indexOf("a") + 2] - '0';
    dv = str[str.indexOf("a") + 3] - '0';
    t1 = tram * 100 + chuc * 10 + dv;
  }
  else if (s == 5) {
    nghin = str[str.indexOf("a") + 1] - '0';
    tram = str[str.indexOf("a") + 2] - '0';
    chuc = str[str.indexOf("a") + 3] - '0';
    dv = str[str.indexOf("a") + 4] - '0';
    t1 = nghin * 1000 + tram * 100 + chuc * 10 + dv;
  }
  else if (s == 6) {
    chucnghin = str[str.indexOf("a") + 1] - '0';
    nghin = str[str.indexOf("a") + 2] - '0';
    tram = str[str.indexOf("a") + 3] - '0';
    chuc = str[str.indexOf("a") + 4] - '0';
    dv = str[str.indexOf("a") + 5] - '0';
    t1 = chucnghin * 10000 + nghin * 1000 + tram * 100 + chuc * 10 + dv;
  }

  s = str.indexOf("c") - str.indexOf("b");
  if (s == 2) {
    t2 = str[str.indexOf("b") + 1] - '0';
  }
  else if (s == 3) {
    chuc = str[str.indexOf("b") + 1] - '0';
    dv = str[str.indexOf("b") + 2] - '0';
    t2 = chuc * 10 + dv;
  }
  else if (s == 4) {
    tram = str[str.indexOf("b") + 1] - '0';
    chuc = str[str.indexOf("b") + 2] - '0';
    dv = str[str.indexOf("b") + 3] - '0';
    t2 = tram * 100 + chuc * 10 + dv;
  }
  else if (s == 5) {
    nghin = str[str.indexOf("b") + 1] - '0';
    tram = str[str.indexOf("b") + 2] - '0';
    chuc = str[str.indexOf("b") + 3] - '0';
    dv = str[str.indexOf("b") + 4] - '0';
    t2 = nghin * 1000 + tram * 100 + chuc * 10 + dv;
  }

  s = str.indexOf("d") - str.indexOf("c");
  if (s == 2) {
    t3 = str[str.indexOf("c") + 1] - '0';
  }
  else if (s == 3) {
    chuc = str[str.indexOf("c") + 1] - '0';
    dv = str[str.indexOf("c") + 2] - '0';
    t3 = chuc * 10 + dv;
  }
  else if (s == 4) {
    tram = str[str.indexOf("c") + 1] - '0';
    chuc = str[str.indexOf("c") + 2] - '0';
    dv = str[str.indexOf("c") + 3] - '0';
    t3 = tram * 100 + chuc * 10 + dv;
  }
  else if (s == 5) {
    nghin = str[str.indexOf("c") + 1] - '0';
    tram = str[str.indexOf("c") + 2] - '0';
    chuc = str[str.indexOf("c") + 3] - '0';
    dv = str[str.indexOf("c") + 4] - '0';
    t3 = nghin * 1000 + tram * 100 + chuc * 10 + dv;
  }

  s = str.indexOf("e") - str.indexOf("d");
  if (s == 2) {
    t4 = str[str.indexOf("d") + 1] - '0';
  }
  else if (s == 3) {
    chuc = str[str.indexOf("d") + 1] - '0';
    dv = str[str.indexOf("d") + 2] - '0';
    t4 = chuc * 10 + dv;
  }
  else if (s == 4) {
    tram = str[str.indexOf("d") + 1] - '0';
    chuc = str[str.indexOf("d") + 2] - '0';
    dv = str[str.indexOf("d") + 3] - '0';
    t4 = tram * 100 + chuc * 10 + dv;
  }
  else if (s == 5) {
    nghin = str[str.indexOf("d") + 1] - '0';
    tram = str[str.indexOf("d") + 2] - '0';
    chuc = str[str.indexOf("d") + 3] - '0';
    dv = str[str.indexOf("d") + 4] - '0';
    t4 = nghin * 1000 + tram * 100 + chuc * 10 + dv;
  }

  s = str.indexOf("f") - str.indexOf("e");
  if (s == 2) {
    t5 = str[str.indexOf("e") + 1] - '0';
  }
  else if (s == 3) {
    chuc = str[str.indexOf("e") + 1] - '0';
    dv = str[str.indexOf("e") + 2] - '0';
    t5 = chuc * 10 + dv;
  }
  else if (s == 4) {
    tram = str[str.indexOf("e") + 1] - '0';
    chuc = str[str.indexOf("e") + 2] - '0';
    dv = str[str.indexOf("e") + 3] - '0';
    t5 = tram * 100 + chuc * 10 + dv;
  }
  else if (s == 5) {
    nghin = str[str.indexOf("e") + 1] - '0';
    tram = str[str.indexOf("e") + 2] - '0';
    chuc = str[str.indexOf("e") + 3] - '0';
    dv = str[str.indexOf("e") + 4] - '0';
    t5 = nghin * 1000 + tram * 100 + chuc * 10 + dv;
  }
} // end xulydulieu
