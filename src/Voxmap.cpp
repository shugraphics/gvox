// Voxmap.cpp

#include "Voxmap.h"
#include <QImage>
#include <QDir>
#include <QDebug>
#include <QDateTime>
#include <QJsonDocument>
#include <math.h>

Voxmap::Voxmap() {
  X = Y = Z = 0;
  ystride = zstride = 0;
  data = 0;
  nullval = 0;
}

Voxmap::~Voxmap() {
  delete data;
}

void Voxmap::traverse(QString src, QStringList &out) {
  QDir dir(src);
  for (auto sub: dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
			       QDir::Name)) {
    traverse(dir.filePath(sub), out);
  }
  QStringList flts;
  flts << "*.jpg";
  flts << "*.jpeg";
  flts << "*.tif";
  flts << "*.tiff";
  flts << "*.png";
  for (auto fn: dir.entryList(flts, QDir::Files  | QDir::NoDotAndDotDot,
			      QDir::Name)) {
    out << dir.filePath(fn);
  }
}

void Voxmap::clear() {
  X = Y = Z = 0;
  delete [] data;
  data = 0;
  meta = QJsonObject();
}  

void Voxmap::importDir(QString source, QString outbase) {
  clear();
  
  QStringList ifns;
  traverse(source, ifns);
  qDebug() << "Input files:" << ifns;
  if (ifns.isEmpty()) {
    return;
  }

  QImage img(ifns[0]);
  X = img.width();
  Y = img.height();
  Z = ifns.size();
  if (X*Y*Z > MAXMVOX*1024*1024) {
    qDebug() << "Image too large";
    clear();
    return;
  }
  
  data = new uint8_t[X*Y*Z];
  ystride = X;
  zstride = X*Y;

  for (int z=0; z<Z; z++) {
    qDebug() << "Loading " << z << "/" << Z << ": " << ifns[z];
    QImage img(ifns[z]);
    if (img.width()!=X || img.height()!=Y) {
      qDebug() << "size mismatch at " << z << ": " << ifns[z];
      clear();
      return;
    }
    memcpy((void*)(data + z*zstride), (void const *)(img.bits()), X*Y);
  }

  meta["width"] = X;
  meta["height"] = Y;
  meta["depth"] = Z;
  meta["source"] = source;
  meta["outbase"] = outbase;
  meta["importdate"] = QDateTime::currentDateTime().toString();

  QJsonObject top;

  qDebug() << "Saving...";

  QFile jsonfile(outbase + ".json");
  if (jsonfile.open(QFile::WriteOnly)) {
    top["voxmap"] = meta;
    QJsonDocument json(top);
    jsonfile.write(json.toJson());
    jsonfile.close();
  } else {    
    qDebug() << "Could not write json";
  } 


  QFile datafile(outbase + ".data");
  if (datafile.open(QFile::WriteOnly)) {
    for (int z=0; z<Z; z++) {
      // qDebug() << "Writing " << z << "/" << Z;
      datafile.write((char const *)(data + z*zstride), zstride);
    }
  } else {
    qDebug() << "Could not write data";
  }
}

void Voxmap::loadFromJson(QString jsonfn) {
  clear();
  QFile jsonfh(jsonfn);
  if (jsonfh.open(QFile::ReadOnly)) {
    QJsonDocument json = QJsonDocument::fromJson(jsonfh.read(1000*1000));
    if (!json.isObject()) {
      qDebug() << "Could not read valid json from" << jsonfn;
      return;
    }
    meta = json.object()["voxmap"].toObject();
    if (meta.isEmpty()) {
      qDebug() << "Could not get voxmap info from" << jsonfn;
      return;
    }
    jsonfh.close();
  } else {
    qDebug() << "Could not read json file" << jsonfn;
    return;
  }

  QFile datafh(meta["outbase"].toString() + ".data");
  if (datafh.open(QFile::ReadOnly)) {
    X = meta["width"].toInt();
    Y = meta["height"].toInt();
    Z = meta["depth"].toInt();

    if (X*Y*Z > MAXMVOX*1024*1024) {
      qDebug() << "Image too large";
      clear();
      return;
    }
    
    ystride = X;
    zstride = X*Y;
    data = new uint8_t[X*Y*Z];
    for (int z=0; z<Z; z++) {
      //      qDebug() << "Reading " << z << "/" << Z;
      if (datafh.read((char *)(data + z*zstride), zstride) < zstride) {
	qDebug() << "Unexpected eof";
	clear();
	return;
      }
    }
    datafh.close();
  } else {
    qDebug() << "Could not read data";
    clear();
  }
}

void Voxmap::setNullValue(uint8_t v) {
  nullval = v;
}

void Voxmap::scanLine(Transform3 const &t, int y, int z, int nx,
                      uint8_t *dest, uint8_t const *lut) {
  Point3 p0 = t.apply(Point3(0, y, z));
  Point3 p1 = t.apply(Point3(nx-1, y, z));
  float x0 = p0.x + .5;
  float y0 = p0.y + .5;
  float z0 = p0.z + .5;
  float x1 = p1.x + .5;
  float y1 = p1.y + .5;
  float z1 = p1.z + .5;
  float dx = t.m[0][0];
  float dy = t.m[1][0];
  float dz = t.m[2][0];
  if (x0>=0 && y0>=0 && z0>=0 && x0<X && y0<Y && z0<Z
      && x1>=0 && y1>=0 && z1>=0 && x1<X && y1<Y && z1<Z) {
    for (int ix=0; ix<nx; ix++) {
      *dest++ = lut[data[int(x0)+int(y0)*ystride+int(z0)*zstride]];
      x0 += dx;
      y0 += dy;
      z0 += dz;
    }
  } else {
    for (int ix=0; ix<nx; ix++) {
      *dest++ = lut[pixelAt(x0, y0, z0)];
      x0 += dx;
      y0 += dy;
      z0 += dz;
    }
  }
}

void Voxmap::scanLineTril(Transform3 const &t, int y, int z, int nx,
			       uint8_t *dest, uint8_t const *lut) {
  Point3 p0 = t.apply(Point3(0, y, z));
  float x0 = p0.x;
  float y0 = p0.y;
  float z0 = p0.z;
  float dx = t.m[0][0];
  float dy = t.m[1][0];
  float dz = t.m[2][0];
  for (int ix=0; ix<nx; ix++) {
    *dest++ = lut[trilinear(x0, y0, z0)];
    x0 += dx;
    y0 += dy;
    z0 += dz;
  }
}


void Voxmap::scanLineTrilDepth(Transform3 const &t, int y, int nx, int nz,
			       uint8_t *dest, uint8_t const *lut) {
  Point3 p0 = t.apply(Point3(0, y, nz-1));
  float x0 = p0.x;
  float y0 = p0.y;
  float z0 = p0.z;
  float dx = t.m[0][0];
  float dy = t.m[1][0];
  float dz = t.m[2][0];
  float dxd = -t.m[0][2];
  float dyd = -t.m[1][2];
  float dzd = -t.m[2][2];
  for (int ix=0; ix<nx; ix++) {
    float alpha = 1;
    float gray = 0;
    float x1 = x0;
    float y1 = y0;
    float z1 = z0;
    for (int iz=0; iz<nz; iz++) {
      float here = trilinear(x1, y1, z1)/255.0;
      float halpha = sqrt(here);
      //alpha = halpha + alpha*(1-halpha);
      gray = here*halpha + gray*(1-halpha);
      x1 += dxd;
      y1 += dyd;
      z1 += dzd;
    }
    *dest++ = lut[uint8_t(255.99*gray/alpha)];
    x0 += dx;
    y0 += dy;
    z0 += dz;
  }
}
