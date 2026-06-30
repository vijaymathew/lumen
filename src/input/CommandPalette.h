#pragma once

#include <QHash>
#include <QString>
#include <QVector>
#include <QWidget>

class QLineEdit;
class QListWidget;

// CommandPalette is the fuzzy "/"-triggered overlay (docs/DESIGN.md §4.2). It
// floats over the dimmed canvas, filters commands by subsequence match, and
// emits commandTriggered() with the chosen command id.
//
// Ranking blends match quality with usage: while you type, the tightest
// subsequence match wins, with frequency/recency breaking ties; on an empty
// query the list doubles as a "most-used first" browse view. Usage is persisted
// across sessions via QSettings.
class CommandPalette : public QWidget {
    Q_OBJECT

public:
    struct Command {
        QString id;
        QString title;
    };

    explicit CommandPalette(QWidget *parent = nullptr);

    void setCommands(const QVector<Command> &commands);

    // Shows the palette, clears the query, and focuses the search field.
    void reveal();
    // Hides the palette and emits dismissed().
    void dismiss();

    // Subsequence fuzzy match. Returns true if every char of `pattern` appears
    // in `text` in order (case-insensitive); `score` is lower for tighter
    // matches. An empty pattern matches everything with score 0.
    static bool fuzzyMatch(const QString &pattern, const QString &text, int *score);

signals:
    void commandTriggered(const QString &id);
    void dismissed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void refilter();
    void triggerCurrent();
    void loadUsage();              // pull persisted counts/recency from QSettings
    void recordUse(const QString &id); // bump + persist on activation

    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
    QVector<Command> m_commands;

    // Usage stats keyed by command id. m_useCount = how often a command was run
    // (frequency); m_lastUsed = a monotonic sequence number set on each run
    // (recency, higher = more recent). m_seq hands out those numbers.
    QHash<QString, int> m_useCount;
    QHash<QString, qint64> m_lastUsed;
    qint64 m_seq = 0;
};
