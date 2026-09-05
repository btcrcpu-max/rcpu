// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/test/uritests.h>

#include <qt/guiutil.h>
#include <qt/walletmodel.h>

#include <QUrl>

void URITests::uriTests()
{
    SendCoinsRecipient rv;
    QUrl uri;
    // !RCPU
    uri.setUrl(QString("rcpu:175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W?req-dontexist="));
    // !RCPU END

    QVERIFY(!GUIUtil::parseBitcoinURI(uri, &rv));

    // !RCPU
    uri.setUrl(QString("rcpu:175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W?dontexist="));
    // !RCPU END
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W"));
    QVERIFY(rv.label == QString());
    QVERIFY(rv.amount == 0);

    // !RCPU
    uri.setUrl(QString("rcpu:175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W?label=Wikipedia Example Address"));
    // !RCPU END
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W"));
    QVERIFY(rv.label == QString("Wikipedia Example Address"));
    QVERIFY(rv.amount == 0);

    // !RCPU
    uri.setUrl(QString("rcpu:175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W?amount=0.001"));
    // !RCPU END
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W"));
    QVERIFY(rv.label == QString());
    QVERIFY(rv.amount == 100000);

    // !RCPU
    uri.setUrl(QString("rcpu:175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W?amount=1.001"));
    // !RCPU END
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W"));
    QVERIFY(rv.label == QString());
    QVERIFY(rv.amount == 100100000);

    // !RCPU
    uri.setUrl(QString("rcpu:175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W?amount=100&label=Wikipedia Example"));
    // !RCPU END
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W"));
    QVERIFY(rv.amount == 10000000000LL);
    QVERIFY(rv.label == QString("Wikipedia Example"));

    // !RCPU
    uri.setUrl(QString("rcpu:175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W?message=Wikipedia Example Address"));
    // !RCPU END
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W"));
    QVERIFY(rv.label == QString());

    // !RCPU
    QVERIFY(GUIUtil::parseBitcoinURI("rcpu:175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W?message=Wikipedia Example Address", &rv));
    // !RCPU END
    QVERIFY(rv.address == QString("175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W"));
    QVERIFY(rv.label == QString());

    // !RCPU
    uri.setUrl(QString("rcpu:175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W?req-message=Wikipedia Example Address"));
    // !RCPU END
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));

    // Commas in amounts are not allowed.
    // !RCPU
    uri.setUrl(QString("rcpu:175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W?amount=1,000&label=Wikipedia Example"));
    // !RCPU END
    QVERIFY(!GUIUtil::parseBitcoinURI(uri, &rv));

    // !RCPU
    uri.setUrl(QString("rcpu:175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W?amount=1,000.0&label=Wikipedia Example"));
    // !RCPU END
    QVERIFY(!GUIUtil::parseBitcoinURI(uri, &rv));

    // There are two amount specifications. The last value wins.
    // !RCPU
    uri.setUrl(QString("rcpu:175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W?amount=100&amount=200&label=Wikipedia Example"));
    // !RCPU END
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W"));
    QVERIFY(rv.amount == 20000000000LL);
    QVERIFY(rv.label == QString("Wikipedia Example"));

    // The first amount value is correct. However, the second amount value is not valid. Hence, the URI is not valid.
    // !RCPU
    uri.setUrl(QString("rcpu:175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W?amount=100&amount=1,000&label=Wikipedia Example"));
    // !RCPU END
    QVERIFY(!GUIUtil::parseBitcoinURI(uri, &rv));

    // Test label containing a question mark ('?').
    // !RCPU
    uri.setUrl(QString("rcpu:175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W?amount=100&label=?"));
    // !RCPU END
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W"));
    QVERIFY(rv.amount == 10000000000LL);
    QVERIFY(rv.label == QString("?"));

    // Escape sequences are not supported.
    // !RCPU
    uri.setUrl(QString("rcpu:175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W?amount=100&label=%3F"));
    // !RCPU END
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W"));
    QVERIFY(rv.amount == 10000000000LL);
    QVERIFY(rv.label == QString("%3F"));
}
