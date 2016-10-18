/**
 * @file tools/encrypt/src/main.cpp
 * @ingroup tools
 *
 * @author COMP Omega <compomega@tutanota.com>
 *
 * @brief Tool to generate a new hashlist.dat for an updater.
 *
 * This tool will generate a new hashlist.dat for an updater.
 *
 * Copyright (C) 2012-2016 COMP_hack Team <compomega@tutanota.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <PushIgnore.h>
#include <QCoreApplication>
#include <QDir>
#include <QMap>
#include <QList>
#include <QFile>
#include <QRegExp>
#include <QDateTime>
#include <QStringList>
#include <QCryptographicHash>

#include <iostream>
#include <zlib.h>
#include <PopIgnore.h>

class FileData
{
public:
    QString path;
    QString compressed_hash;
    QString uncompressed_hash;

    int compressed_size;
    int uncompressed_size;
};

int compressChunk(const void *src, void *dest, int chunk_size,
    int out_size, int level)
{
    z_stream strm;

    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    if(deflateInit(&strm, level >= 0 ? level : Z_DEFAULT_COMPRESSION) != Z_OK)
    {
        printf("Call to deflateInit failed.\n");
        if(strm.msg)
            printf("Zlib reported the following: %s\n", strm.msg);

        return 0;
    }

    strm.avail_in = chunk_size;
    strm.next_in = (Bytef*)src;

    strm.avail_out = out_size;
    strm.next_out = (Bytef*)dest;

    if(deflate(&strm, Z_FINISH) != Z_STREAM_END)
    {
        printf("Call to deflate failed.\n");
        if(strm.msg)
            printf("Zlib reported the following: %s\n", strm.msg);

        return 0;
    }

    int written = out_size - strm.avail_out;

    if(deflateEnd(&strm) != Z_OK)
    {
        printf("Call to deflateEnd failed.\n");
        if(strm.msg)
            printf("Zlib reported the following: %s\n", strm.msg);

        return 0;
    }

    return written;
}

QMap<QString, FileData*> parseFileList(const QByteArray& d)
{
    QMap<QString, FileData*> files;

    QStringList lines = QString::fromUtf8(d).split("\n");
    foreach(QString line, lines)
    {
        QRegExp fileMatcher("FILE : (.+),([0-9a-fA-F]{32}),([0-9]+),"
            "([0-9a-fA-F]{32}),([0-9]+)");

        if( !fileMatcher.exactMatch( line.trimmed() ) )
            continue;

        FileData *info = new FileData;
        info->path = fileMatcher.cap(1).mid(2).replace("\\", "/");
        info->compressed_hash = fileMatcher.cap(2).toUpper();
        info->compressed_size = fileMatcher.cap(3).toInt();
        info->uncompressed_hash = fileMatcher.cap(4).toUpper();
        info->uncompressed_size = fileMatcher.cap(5).toInt();

        files[info->path] = info;
    }

    return files;
}

QStringList recursiveEntryList(const QString& dir, QDir::SortFlags sort)
{
    QStringList files;

    QStringList temp_files = QDir(dir).entryList(QDir::Files, sort);

    foreach(QString f, temp_files)
        files << QString("%1/%2").arg(dir).arg(f);

    QStringList dirs = QDir(dir).entryList(QDir::Dirs |
        QDir::NoDotAndDotDot, sort);

    foreach(QString d, dirs)
    {
        files << recursiveEntryList(QString("%1/%2").arg(dir).arg(d), sort);
    }

    return files;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QStringList args = app.arguments();
    args.removeFirst();

    if(args.count() != 4 || args.at(0) != "--base" ||
        args.at(2) != "--overlay")
    {
        std::cerr << "SYNTAX: comp_rehash --base BASE --overlay OVERLAY"
            << std::endl;

        return -1;
    }

    QString base = args.at(1);
    QString overlay = args.at(3);

    // Read in the original file list
    QMap<QString, FileData*> files;
    {
        // Open the hashlist.dat
        QFile hashlist(QString("%1/hashlist.dat").arg(base));
        if( !hashlist.open(QIODevice::ReadOnly) )
        {
            std::cerr << "Failed to open the hashlist.dat file "
                "for reading." << std::endl;

            return -1;
        }

        // Read and parse the hashlist.dat
        files = parseFileList( hashlist.readAll() );

        // Close the hashlist.dat file
        hashlist.close();
    }

    // Find each file in the overlay
    foreach(QString file, recursiveEntryList(overlay, QDir::Name))
    {
        if( QRegExp(".+\\.compressed").exactMatch(file) )
            continue;

        QString shortName = file.mid(overlay.length() + 1);
        QString shortComp = QString("%1.compressed").arg(shortName);

        if(shortName == "hashlist.dat")
            continue;
        if(shortName == "hashlist.ver")
            continue;

        if( files.contains(shortComp) )
        {
            delete files.value(shortComp);
        }
        if( files.contains(shortName) )
        {
            delete files.value(shortName);

            files.remove(shortName);
        }

        FileData *d = new FileData;
        d->path = shortComp;

        QByteArray uncomp_data;
        {
            QFile handle(file);
            handle.open(QIODevice::ReadOnly);

            uncomp_data = handle.readAll();
        }

        d->uncompressed_hash = QCryptographicHash::hash(uncomp_data,
            QCryptographicHash::Md5).toHex();
        d->uncompressed_size = uncomp_data.length();

        //
        // Process the compressed copy now
        //

        // Calculate max size
        int out_size = (int)((float)uncomp_data.size() * 0.001f + 0.5f);
        out_size += uncomp_data.size() + 12;

        char *out_buffer = new char[out_size];

        int sz = compressChunk(uncomp_data.constData(), out_buffer,
            uncomp_data.size(), out_size, 9);

        QByteArray comp_data(out_buffer, sz);

        delete[] out_buffer;

        // Write the compressed copy
        {
            QFile handle(file + ".compressed");
            handle.open(QIODevice::WriteOnly);
            handle.write(comp_data);
        }

        d->compressed_hash = QCryptographicHash::hash(comp_data,
            QCryptographicHash::Md5).toHex();
        d->compressed_size = comp_data.length();

        files[shortComp] = d;
    }

    QFile hashlist(QString("%1/hashlist.dat").arg(overlay));
    hashlist.open(QIODevice::WriteOnly);

    foreach(FileData *d, files)
    {
        if(d->path == "ImagineUpdate.dat.compressed")
            continue;

        QString line = "FILE : .\\%1,%2,%3,%4,%5 ";
        line = line.arg(d->path.replace("/", "\\"));
        line = line.arg(d->compressed_hash.toUpper());
        line = line.arg(d->compressed_size);
        line = line.arg(d->uncompressed_hash.toUpper());
        line = line.arg(d->uncompressed_size);

        hashlist.write(line.toUtf8());
        hashlist.write("\r\n", 2);
    }

    QString exe = "EXE : .\\ImagineClient.exe ";
    QString ver = "VERSION : %1";
    ver = ver.arg( QDateTime::currentDateTime().toString("yyyyMMddhhmmss") );

    hashlist.write(exe.toUtf8());
    hashlist.write("\r\n", 2);
    hashlist.write(ver.toUtf8());
    hashlist.close();

    QFile hashver(QString("%1/hashlist.ver").arg(overlay));
    hashver.open(QIODevice::WriteOnly);
    hashver.write(ver.toUtf8());
    hashver.close();

    //
    // Compress the hashlist.dat
    //
    {
        // Open the hashlist.dat
        hashlist.open(QIODevice::ReadOnly);

        // Read in the hashlist.dat
        QByteArray uncomp_data = hashlist.readAll();

        // Close the hashlist.dat
        hashlist.close();

        // Calculate max size
        int out_size = (int)((float)uncomp_data.size() * 0.001f + 0.5f);
        out_size += uncomp_data.size() + 12;

        char *out_buffer = new char[out_size];

        int sz = compressChunk(uncomp_data.constData(), out_buffer,
            uncomp_data.size(), out_size, 9);

        QByteArray comp_data(out_buffer, sz);

        delete[] out_buffer;

        // Write the compressed copy
        {
            QFile handle(QString("%1/hashlist.dat.compressed").arg(overlay));
            handle.open(QIODevice::WriteOnly);
            handle.write(comp_data);
        }
    }

    return 0;
}
