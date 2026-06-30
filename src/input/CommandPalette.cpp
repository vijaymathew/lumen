#include "input/CommandPalette.h"

#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QSettings>
#include <QVBoxLayout>

#include <algorithm>

CommandPalette::CommandPalette(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("commandPalette"));
    // A bare QWidget does not paint a stylesheet `background` unless this is set.
    // Without it the panel is transparent and only the search field and the
    // selected row (which paint their own backgrounds) are legible — the rest of
    // the list sits directly on the photo, so readability depends on the image.
    setAttribute(Qt::WA_StyledBackground, true);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(QStringLiteral("Search commands — ↑↓ to browse, ↵ to run"));
    m_search->installEventFilter(this);

    loadUsage();

    m_list = new QListWidget(this);
    m_list->setFocusPolicy(Qt::NoFocus);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);
    layout->addWidget(m_search);
    layout->addWidget(m_list);

    // Dark styling to match the immersive canvas (mockups/latent_editor_mockup).
    setStyleSheet(QStringLiteral(R"(
        #commandPalette {
            background: #1c1c1f;
            border: 1px solid #38383d;
            border-radius: 10px;
        }
        QLineEdit {
            background: #141416;
            border: 1px solid #38383d;
            border-radius: 6px;
            padding: 8px 10px;
            color: #e8e8ea;
            font-size: 14px;
        }
        QListWidget {
            background: #1c1c1f;
            border: none;
            color: #c8c8cc;
            font-size: 13px;
        }
        QListWidget::item { padding: 7px 10px; border-radius: 6px; }
        QListWidget::item:selected { background: #2a2a2e; color: #f0f0f1; }
    )"));

    connect(m_search, &QLineEdit::textChanged, this, [this] { refilter(); });
    connect(m_list, &QListWidget::itemActivated, this, [this] { triggerCurrent(); });

    hide();
}

void CommandPalette::setCommands(const QVector<Command> &commands)
{
    m_commands = commands;
    refilter();
}

void CommandPalette::reveal()
{
    m_search->clear();
    refilter();
    show();
    raise();
    m_search->setFocus(Qt::ShortcutFocusReason);
}

void CommandPalette::dismiss()
{
    if (!isVisible())
        return;
    hide();
    emit dismissed();
}

bool CommandPalette::fuzzyMatch(const QString &pattern, const QString &text, int *score)
{
    if (pattern.isEmpty()) {
        if (score)
            *score = 0;
        return true;
    }

    const QString p = pattern.toLower();
    const QString t = text.toLower();

    int ti = 0;
    int total = 0;
    int lastMatch = -1;
    for (const QChar pc : p) {
        const int idx = t.indexOf(pc, ti);
        if (idx < 0)
            return false;
        // Penalise gaps between consecutive matched characters; reward matches
        // at the very start.
        const int gap = idx - (lastMatch + 1);
        total += gap + (idx == 0 ? -2 : 0);
        lastMatch = idx;
        ti = idx + 1;
    }
    if (score)
        *score = total;
    return true;
}

void CommandPalette::refilter()
{
    const QString query = m_search ? m_search->text() : QString();
    const bool browsing = query.isEmpty();

    struct Scored {
        int order;
        int score;
        int count;
        qint64 lastUsed;
        Command cmd;
    };
    QVector<Scored> matched;
    matched.reserve(m_commands.size());

    for (int i = 0; i < m_commands.size(); ++i) {
        int score = 0;
        if (fuzzyMatch(query, m_commands[i].title, &score)) {
            const QString &id = m_commands[i].id;
            matched.push_back(
                {i, score, m_useCount.value(id, 0), m_lastUsed.value(id, 0), m_commands[i]});
        }
    }

    std::stable_sort(matched.begin(), matched.end(), [browsing](const Scored &a, const Scored &b) {
        if (browsing) {
            // Empty query is a "show everything, most-used first" browse view:
            // recency leads, frequency breaks ties, declared order is the floor.
            if (a.lastUsed != b.lastUsed)
                return a.lastUsed > b.lastUsed;
            if (a.count != b.count)
                return a.count > b.count;
            return a.order < b.order;
        }
        // While typing, match quality dominates; usage only breaks equal-quality
        // ties so the tool you reach for most surfaces first.
        if (a.score != b.score)
            return a.score < b.score;
        if (a.count != b.count)
            return a.count > b.count;
        if (a.lastUsed != b.lastUsed)
            return a.lastUsed > b.lastUsed;
        return a.order < b.order;
    });

    m_list->clear();
    for (const Scored &s : matched) {
        auto *item = new QListWidgetItem(s.cmd.title, m_list);
        item->setData(Qt::UserRole, s.cmd.id);
    }
    if (m_list->count() > 0)
        m_list->setCurrentRow(0);
}

void CommandPalette::triggerCurrent()
{
    QListWidgetItem *item = m_list->currentItem();
    if (!item)
        return;
    const QString id = item->data(Qt::UserRole).toString();
    recordUse(id);
    dismiss();
    emit commandTriggered(id);
}

void CommandPalette::loadUsage()
{
    QSettings s;
    s.beginGroup(QStringLiteral("paletteUsage"));
    m_seq = s.value(QStringLiteral("seq"), 0).toLongLong();
    const int n = s.beginReadArray(QStringLiteral("commands"));
    for (int i = 0; i < n; ++i) {
        s.setArrayIndex(i);
        const QString id = s.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;
        m_useCount.insert(id, s.value(QStringLiteral("count")).toInt());
        m_lastUsed.insert(id, s.value(QStringLiteral("lastUsed")).toLongLong());
    }
    s.endArray();
    s.endGroup();
}

void CommandPalette::recordUse(const QString &id)
{
    m_useCount[id] = m_useCount.value(id, 0) + 1;
    m_lastUsed[id] = ++m_seq;

    // Persist the whole table — it is tiny (one row per distinct command run).
    QSettings s;
    s.beginGroup(QStringLiteral("paletteUsage"));
    s.setValue(QStringLiteral("seq"), m_seq);
    s.beginWriteArray(QStringLiteral("commands"), m_useCount.size());
    int i = 0;
    for (auto it = m_useCount.constBegin(); it != m_useCount.constEnd(); ++it, ++i) {
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("id"), it.key());
        s.setValue(QStringLiteral("count"), it.value());
        s.setValue(QStringLiteral("lastUsed"), m_lastUsed.value(it.key(), 0));
    }
    s.endArray();
    s.endGroup();
}

bool CommandPalette::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_search && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        switch (ke->key()) {
        case Qt::Key_Escape:
            dismiss();
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            triggerCurrent();
            return true;
        case Qt::Key_Down:
            if (m_list->count() > 0)
                m_list->setCurrentRow(std::min(m_list->currentRow() + 1, m_list->count() - 1));
            return true;
        case Qt::Key_Up:
            if (m_list->count() > 0)
                m_list->setCurrentRow(std::max(m_list->currentRow() - 1, 0));
            return true;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}
