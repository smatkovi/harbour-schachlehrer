// Qt 4 replacement for QUrlQuery, which is Qt 5. In Qt 4 the query lives on
// QUrl itself, so this is a thin shell over those very methods -- enough for
// reading a query out of a URL and for building an application/x-www-form-
// urlencoded body.
#pragma once
#include <QString>
#include <QUrl>

class QUrlQuery
{
public:
    QUrlQuery() {}
    explicit QUrlQuery(const QUrl& url) { m_url.setEncodedQuery(url.encodedQuery()); }
    explicit QUrlQuery(const QString& query)
    { m_url.setEncodedQuery(query.toUtf8()); }

    void addQueryItem(const QString& key, const QString& value)
    { m_url.addQueryItem(key, value); }
    bool hasQueryItem(const QString& key) const { return m_url.hasQueryItem(key); }
    // The second argument is Qt 5's component formatting; Qt 4 decodes
    // already, so it is accepted and ignored.
    QString queryItemValue(const QString& key, int = 0) const
    { return m_url.queryItemValue(key); }
    void removeQueryItem(const QString& key) { m_url.removeQueryItem(key); }
    bool isEmpty() const { return m_url.encodedQuery().isEmpty(); }

    QString query() const { return QString::fromUtf8(m_url.encodedQuery()); }
    QString toString(int = 0) const { return QString::fromUtf8(m_url.encodedQuery()); }

private:
    QUrl m_url;
};
