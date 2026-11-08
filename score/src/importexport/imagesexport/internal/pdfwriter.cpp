/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "pdfwriter.h"

#include <QBuffer>
#include <QPdfWriter>
#include <QPainter>

#include "settings.h"
#include "inkprint.h"

#include "notation/inotation.h"
#include "notation/inotationpainting.h"

#include "log.h"

using namespace mu::iex::imagesexport;
using namespace mu::project;
using namespace mu::notation;
using namespace muse;
using namespace muse::io;
using namespace muse::draw;
using namespace mu::engraving;


// Score (Laurence Who Studio): every PDF comes out with the printed (InkPrint) look.
// Turn off with the setting export/pdf/printedLook = false.
static bool printedLookEnabled()
{
    static const muse::Settings::Key KEY("iex_imagesexport", "export/pdf/printedLook");
    const muse::Val v = muse::settings()->value(KEY);
    return v.isNull() ? true : v.toBool();
}

static int printedLookDpi()
{
    static const muse::Settings::Key KEY("iex_imagesexport", "export/pdf/printedLookDpi");
    const muse::Val v = muse::settings()->value(KEY);
    return v.isNull() ? 600 : std::clamp(v.toInt(), 300, 1200);
}

//! Renders each page, applies InkPrint and places it as the page image.
static void paintPrinted(QPdfWriter& pdfWriter, QPainter& qp, const INotationPtr& notation, int fromPage, int toPage, bool& firstPage)
{
    const double dpi = printedLookDpi();
    const int pages = notation->painting()->pageCount();
    if (fromPage < 0) {
        fromPage = 0;
        toPage = pages - 1;
    }
    for (int p = fromPage; p <= toPage && p < pages; ++p) {
        INotationPainting::Options opt;
        opt.fromPage = p;
        opt.toPage = p;
        opt.deviceDpi = dpi;
        opt.printPageBackground = false;

        const muse::SizeF inch = notation->painting()->pageSizeInch(opt);
        QImage image(std::lrint(inch.width() * dpi), std::lrint(inch.height() * dpi), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        {
            muse::draw::Painter painter(&image, "inkprint");
            notation->painting()->paintPng(&painter, opt);
            painter.endDraw();
        }
        const QImage printed = inkPrint(image, dpi, 1.0, 259u + unsigned(p));
        image = QImage();

        if (!firstPage) {
            pdfWriter.setPageSize(QPageSize(inch.toQSizeF(), QPageSize::Inch));
            pdfWriter.newPage();
        }
        firstPage = false;
        qp.drawImage(QRectF(0, 0, pdfWriter.width(), pdfWriter.height()), printed);
    }
}

std::vector<INotationWriter::UnitType> PdfWriter::supportedUnitTypes() const
{
    return { UnitType::PER_PART, UnitType::MULTI_PART };
}

Ret PdfWriter::write(INotationPtr notation, io::IODevice& destinationDevice, const Options& options)
{
    UnitType unitType = unitTypeFromOptions(options);
    IF_ASSERT_FAILED(unitType == UnitType::PER_PART) {
        return Ret(Ret::Code::NotSupported);
    }

    IF_ASSERT_FAILED(notation) {
        return make_ret(Ret::Code::UnknownError);
    }

    QByteArray qdata;
    QBuffer buf(&qdata);
    buf.open(QIODevice::WriteOnly);

    QPdfWriter pdfWriter(&buf);
    preparePdfWriter(pdfWriter, notation->projectWorkTitleAndPartName(), notation->painting()->pageSizeInch().toQSizeF());

    if (printedLookEnabled()) {
        int page = -1;
        auto it = options.find(OptionKey::PAGE_NUMBER);
        if (it != options.end()) {
            page = it->second.toInt();
        }
        bool first = true;
        QPainter qp(&pdfWriter);
        paintPrinted(pdfWriter, qp, notation, page, page, first);
        qp.end();
        buf.close();
        destinationDevice.write(ByteArray::fromQByteArrayNoCopy(qdata));
        return true;
    }

    Painter painter(&pdfWriter, "pdfwriter");
    if (!painter.isActive()) {
        return false;
    }

    const bool TRANSPARENT_BACKGROUND = muse::value(options, OptionKey::TRANSPARENT_BACKGROUND,
                                                    Val(configuration()->exportPdfWithTransparentBackground())).toBool();

    INotationPainting::Options opt;
    opt.deviceDpi = pdfWriter.logicalDpiX();
    opt.onNewPage = [&pdfWriter]() { pdfWriter.newPage(); };
    opt.printPageBackground = !TRANSPARENT_BACKGROUND;

    auto pageNumIt = options.find(OptionKey::PAGE_NUMBER);
    if (pageNumIt != options.end()) {
        opt.fromPage = pageNumIt->second.toInt();
        opt.toPage = opt.fromPage;
    }

    notation->painting()->paintPdf(&painter, opt);

    painter.endDraw();

    ByteArray data = ByteArray::fromQByteArrayNoCopy(qdata);
    destinationDevice.write(data);

    return true;
}

Ret PdfWriter::writeList(const INotationPtrList& notations, io::IODevice& destinationDevice, const Options& options)
{
    IF_ASSERT_FAILED(!notations.empty()) {
        return make_ret(Ret::Code::UnknownError);
    }

    UnitType unitType = unitTypeFromOptions(options);
    IF_ASSERT_FAILED(unitType == UnitType::MULTI_PART) {
        return Ret(Ret::Code::NotSupported);
    }

    INotationPtr firstNotation = notations.front();
    IF_ASSERT_FAILED(firstNotation) {
        return make_ret(Ret::Code::UnknownError);
    }

    QByteArray qdata;
    QBuffer buf(&qdata);
    buf.open(QIODevice::WriteOnly);

    QPdfWriter pdfWriter(&buf);
    preparePdfWriter(pdfWriter, firstNotation->projectWorkTitle(), firstNotation->painting()->pageSizeInch().toQSizeF());

    if (printedLookEnabled()) {
        bool first = true;
        QPainter qp(&pdfWriter);
        for (const auto& n : notations) {
            if (n) {
                paintPrinted(pdfWriter, qp, n, -1, -1, first);
            }
        }
        qp.end();
        buf.close();
        destinationDevice.write(ByteArray::fromQByteArrayNoCopy(qdata));
        return true;
    }

    Painter painter(&pdfWriter, "pdfwriter");
    if (!painter.isActive()) {
        return false;
    }

    const bool TRANSPARENT_BACKGROUND = muse::value(options, OptionKey::TRANSPARENT_BACKGROUND,
                                                    Val(configuration()->exportPdfWithTransparentBackground())).toBool();

    INotationPainting::Options opt;
    opt.deviceDpi = pdfWriter.logicalDpiX();
    opt.onNewPage = [&pdfWriter]() { pdfWriter.newPage(); };
    opt.printPageBackground = !TRANSPARENT_BACKGROUND;

    for (const auto& notation : notations) {
        IF_ASSERT_FAILED(notation) {
            return make_ret(Ret::Code::UnknownError);
        }

        if (notation != firstNotation) {
            QSizeF size = notation->painting()->pageSizeInch().toQSizeF();
            pdfWriter.setPageSize(QPageSize(size, QPageSize::Inch));
            pdfWriter.newPage();
        }

        notation->painting()->paintPdf(&painter, opt);
    }

    painter.endDraw();

    ByteArray data = ByteArray::fromQByteArrayNoCopy(qdata);
    destinationDevice.write(data);

    return true;
}

void PdfWriter::preparePdfWriter(QPdfWriter& pdfWriter, const QString& title, const QSizeF& size) const
{
    pdfWriter.setResolution(configuration()->exportPdfDpiResolution());
    pdfWriter.setCreator(QString("MuseScore Studio Version: ") + application()->version().toString().toQString());
    pdfWriter.setTitle(title);
    pdfWriter.setPageMargins(QMarginsF());
    pdfWriter.setPageLayout(QPageLayout(QPageSize(size, QPageSize::Inch), QPageLayout::Orientation::Portrait, QMarginsF()));

    if (configuration()->exportPdfWithGrayscale()) {
        pdfWriter.setColorModel(QPdfWriter::ColorModel::Grayscale);
    } else {
        pdfWriter.setColorModel(QPdfWriter::ColorModel::Auto);
    }
}
