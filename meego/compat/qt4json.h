// Qt 4 replacement for the QtCore JSON classes, as far as this app uses them.
// QtScript's JSON object (ECMAScript 5, present in Qt 4.7) does the parsing
// and serialising, so nothing extra is needed on the device.
//
// Everything is carried as QVariant underneath, which is also how the Qt 5
// classes behave for these operations, so the call sites are unchanged.
#pragma once
#include <QByteArray>
#include <QList>
#include <QScriptEngine>
#include <QScriptValue>
#include <QScriptValueList>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

class QJsonArray;
class QJsonObject;

class QJsonValue
{
public:
    QJsonValue() {}
    explicit QJsonValue(const QVariant& v) : m_v(v) {}
    // Qt 5 converts implicitly on the way in; the call sites rely on it.
    QJsonValue(const QString& v) : m_v(v) {}
    QJsonValue(const char* v) : m_v(QString::fromUtf8(v)) {}
    QJsonValue(int v) : m_v(v) {}
    QJsonValue(qint64 v) : m_v(v) {}
    QJsonValue(double v) : m_v(v) {}
    QJsonValue(bool v) : m_v(v) {}
    inline QJsonValue(const QJsonArray& v);
    inline QJsonValue(const QJsonObject& v);

    bool isNull() const { return !m_v.isValid() || m_v.isNull(); }
    bool isObject() const { return m_v.type() == QVariant::Map; }
    bool isArray() const { return m_v.type() == QVariant::List; }

    QString toString(const QString& def = QString()) const
    { return m_v.isValid() ? m_v.toString() : def; }
    int toInt(int def = 0) const { return m_v.isValid() ? m_v.toInt() : def; }
    double toDouble(double def = 0) const { return m_v.isValid() ? m_v.toDouble() : def; }
    bool toBool(bool def = false) const { return m_v.isValid() ? m_v.toBool() : def; }
    QStringList toStringList() const { return m_v.toStringList(); }
    QVariant toVariant() const { return m_v; }

    inline QJsonObject toObject() const;
    inline QJsonArray toArray() const;

private:
    QVariant m_v;
};

class QJsonArray
{
public:
    typedef QList<QJsonValue>::const_iterator const_iterator;
    typedef const_iterator iterator;

    QJsonArray() {}
    explicit QJsonArray(const QVariantList& list)
    {
        for (int i = 0; i < list.size(); ++i)
            m_items.append(QJsonValue(list.at(i)));
    }

    static QJsonArray fromStringList(const QStringList& list)
    {
        QJsonArray out;
        for (int i = 0; i < list.size(); ++i)
            out.append(QJsonValue(list.at(i)));
        return out;
    }

    int size() const { return m_items.size(); }
    int count() const { return m_items.size(); }
    bool isEmpty() const { return m_items.isEmpty(); }
    QJsonValue at(int i) const { return m_items.at(i); }
    void append(const QJsonValue& v) { m_items.append(v); }

    // Range-for over the array is how the lesson index is read.
    const_iterator begin() const { return m_items.constBegin(); }
    const_iterator end() const { return m_items.constEnd(); }
    const_iterator constBegin() const { return m_items.constBegin(); }
    const_iterator constEnd() const { return m_items.constEnd(); }

    QVariantList toVariantList() const
    {
        QVariantList out;
        for (int i = 0; i < m_items.size(); ++i)
            out.append(m_items.at(i).toVariant());
        return out;
    }

private:
    QList<QJsonValue> m_items;
};

class QJsonObject
{
public:
    QJsonObject() {}
    explicit QJsonObject(const QVariantMap& map) : m_map(map) {}
    static QJsonObject fromVariantMap(const QVariantMap& map) { return QJsonObject(map); }

    QVariantMap toVariantMap() const { return m_map; }
    bool contains(const QString& key) const { return m_map.contains(key); }
    bool isEmpty() const { return m_map.isEmpty(); }
    QStringList keys() const { return m_map.keys(); }
    QJsonValue value(const QString& key) const { return QJsonValue(m_map.value(key)); }
    QJsonValue operator[](const QString& key) const { return value(key); }
    void insert(const QString& key, const QJsonValue& v) { m_map.insert(key, v.toVariant()); }

private:
    QVariantMap m_map;
};

inline QJsonObject QJsonValue::toObject() const { return QJsonObject(m_v.toMap()); }
inline QJsonArray QJsonValue::toArray() const { return QJsonArray(m_v.toList()); }
inline QJsonValue::QJsonValue(const QJsonArray& v) : m_v(v.toVariantList()) {}
inline QJsonValue::QJsonValue(const QJsonObject& v) : m_v(v.toVariantMap()) {}

// Qt 5 reports the position and a readable message; QtScript's SyntaxError
// carries the message but no offset, so the offset stays 0 rather than being
// invented.
class QJsonParseError
{
public:
    enum ParseError { NoError = 0, GarbageAtEnd = 9 };
    QJsonParseError() : error(NoError), offset(0) {}
    QString errorString() const { return error == NoError ? QString::fromLatin1("no error") : m_message; }

    ParseError error;
    int offset;
    QString m_message;
};

class QJsonDocument
{
public:
    enum JsonFormat { Indented, Compact };

    QJsonDocument() : m_isObject(false), m_isArray(false), m_isNull(true) {}
    explicit QJsonDocument(const QJsonObject& object)
        : m_object(object), m_isObject(true), m_isArray(false), m_isNull(false) {}
    explicit QJsonDocument(const QJsonArray& array)
        : m_array(array), m_isObject(false), m_isArray(true), m_isNull(false) {}

    bool isNull() const { return m_isNull; }
    bool isObject() const { return m_isObject; }
    bool isArray() const { return m_isArray; }
    QJsonObject object() const { return m_object; }
    QJsonArray array() const { return m_array; }

    QByteArray toJson(JsonFormat = Compact) const
    {
        QScriptEngine& e = engine();
        QScriptValue json = e.globalObject().property(QString::fromLatin1("JSON"));
        QScriptValue text = json.property(QString::fromLatin1("stringify"))
            .call(json, QScriptValueList()
                  << (m_isArray ? e.toScriptValue(m_array.toVariantList())
                                : e.toScriptValue(m_object.toVariantMap())));
        if (e.hasUncaughtException()) {
            e.clearExceptions();
            return QByteArray("{}");
        }
        return text.toString().toUtf8();
    }

    static QJsonDocument fromJson(const QByteArray& text, QJsonParseError* error = 0)
    {
        QJsonDocument document;
        QScriptEngine& e = engine();
        QScriptValue json = e.globalObject().property(QString::fromLatin1("JSON"));
        QScriptValue value = json.property(QString::fromLatin1("parse"))
            .call(json, QScriptValueList()
                  << QScriptValue(QString::fromUtf8(text.constData(), text.size())));
        if (e.hasUncaughtException()) {
            if (error) {
                error->error = QJsonParseError::GarbageAtEnd;
                error->m_message = e.uncaughtException().toString();
            }
            e.clearExceptions();
            return document;
        }
        if (!value.isObject() && !value.isArray()) {
            if (error) {
                error->error = QJsonParseError::GarbageAtEnd;
                error->m_message = QString::fromLatin1("not a JSON object or array");
            }
            return document;
        }
        if (error)
            *error = QJsonParseError();
        if (value.isArray()) {
            document.m_array = QJsonArray(value.toVariant().toList());
            document.m_isArray = true;
        } else {
            document.m_object = QJsonObject::fromVariantMap(value.toVariant().toMap());
            document.m_isObject = true;
        }
        document.m_isNull = false;
        return document;
    }

private:
    static QScriptEngine& engine()
    {
        static QScriptEngine instance;
        return instance;
    }

    QJsonObject m_object;
    QJsonArray m_array;
    bool m_isObject;
    bool m_isArray;
    bool m_isNull;
};
