#include "input/CommandPalette.h"

#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

#include <algorithm>

CommandPalette::CommandPalette(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("commandPalette"));

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(QStringLiteral("Type a command…"));
    m_search->installEventFilter(this);

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
            background: transparent;
            border: none;
            color: #b4b4b8;
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

    struct Scored {
        int order;
        int score;
        Command cmd;
    };
    QVector<Scored> matched;
    matched.reserve(m_commands.size());

    for (int i = 0; i < m_commands.size(); ++i) {
        int score = 0;
        if (fuzzyMatch(query, m_commands[i].title, &score))
            matched.push_back({i, score, m_commands[i]});
    }

    std::stable_sort(matched.begin(), matched.end(), [](const Scored &a, const Scored &b) {
        if (a.score != b.score)
            return a.score < b.score;
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
    dismiss();
    emit commandTriggered(id);
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
