// SPDX-FileCopyrightText: 2021 Carson Black <uhhadd@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "chatsmodel.h"
#include "chatsmodel_p.h"

#include "chatsstore.h"
#include "chatsstore_p.h"

#include "messagesmodel.h"

#include "overloader.h"

#include <KLocalizedString>

#include <td/telegram/td_api.h>
#include <td/tl/TlObject.h>

enum Roles {
    ID = Qt::UserRole,
};

ChatsModel::ChatsModel(Client* parent) : QAbstractListModel(parent), c(parent), d(new Private)
{
}

ChatsModel::~ChatsModel()
{
}

void ChatsModel::fetch()
{
    c->call<TDApi::getChats>(
        [](TDApi::getChats::ReturnType resp) {
        },
        nullptr, std::numeric_limits<std::int64_t>::max(), 0, 20
    );
}

void ChatsModel::newChat(TDApi::object_ptr<TDApi::chat> ch)
{
    beginInsertRows(QModelIndex(), d->chats.size(), d->chats.size());
    d->chats.push_back(ch->id_);
    endInsertRows();

    c->chatsStore()->newChat(std::move(ch));
}

bool ChatsModel::canFetchMore(const QModelIndex& parent) const
{
    return !d->atEnd;
}

void ChatsModel::fetchMore(const QModelIndex& parent)
{
    fetch();
}

void ChatsModel::updatePositions(TDApi::int53 chatID, TDApi::object_ptr<TDApi::chatPosition>& pos)
{
    auto& data = c->chatsStore()->d->chatData[chatID];

    std::size_t i = 0;
    for (i = 0; i < data->positions_.size(); i++) {
        if (data->positions_[i]->list_->get_id() == pos->list_->get_id()) {
            break;
        }
    }

    TDApi::array<TDApi::object_ptr<TDApi::chatPosition>> newPositions;
    newPositions.resize(data->positions_.size() + (pos->order_ == 0 ? 0 : 1) - (i < data->positions_.size() ? 1 : 0));
    int ps = 0;
    if (pos->order_ != 0) {
        newPositions[ps++] = std::move(pos);
    }
    for (std::size_t j = 0; j < data->positions_.size(); j++) {
        if (j != i) {
            newPositions[ps++] = std::move(data->positions_[j]);
        }
    }
    Q_ASSERT(ps == newPositions.size());

    data->positions_ = std::move(newPositions);
    Q_EMIT c->chatsStore()->keyDataChanged(QString::number(chatID), {});
}

void ChatsModel::handleUpdate(TDApi::object_ptr<TDApi::Update> u)
{
    TDApi::downcast_call(*u,
        overloaded(
            [this](TDApi::updateNewChat &update_new_chat) {
                newChat(std::move(update_new_chat.chat_));
            },
            [this, &u](TDApi::updateChatLastMessage &update_chat_last_message) {
                auto chatID = update_chat_last_message.chat_id_;
                auto& poses = update_chat_last_message.positions_;
                for (auto& pos : poses) {
                    updatePositions(chatID, pos);
                }
                c->chatsStore()->handleUpdate(std::move(u));
            },
            [this](TDApi::updateChatPosition &update_chat_pos) {
                auto chatID = update_chat_pos.chat_id_;
                auto pos = std::move(update_chat_pos.position_);
                updatePositions(chatID, pos);
            },
            [this, &u](auto& update) { c->chatsStore()->handleUpdate(std::move(u)); }));
}

int ChatsModel::rowCount(const QModelIndex& parent) const
{
    return d->chats.size();
}

QHash<int,QByteArray> ChatsModel::roleNames() const
{
    QHash<int,QByteArray> roles;

    roles[int(Roles::ID)] = "mID";

    return roles;
}

QVariant ChatsModel::data(const QModelIndex& idx, int role) const
{
    if (!checkIndex(idx, CheckIndexOption::IndexIsValid)) {
        return QVariant();
    }

    auto chatID = d->chats[idx.row()];
    auto r = Roles(role);

    switch (r) {
    case Roles::ID: {
        return QString::number(chatID);
    }
    }

    return QVariant();
}

QIviPendingReplyBase ChatsModel::createChat(const QString& name, const QString& type)
{
    QIviPendingReply<bool> ret;

    if (type == "publicGroup" or type == "channel") {
        c->call<TDApi::createNewSupergroupChat>(
            [ret](TDApi::createNewSupergroupChat::ReturnType r) mutable {
                ret.setSuccess(true);
            },
            name.toStdString(), type == "channel", "", nullptr, false
        );
        return ret;
    }

    c->call<TDApi::createNewBasicGroupChat>(
        [ret](TDApi::createNewBasicGroupChat::ReturnType r) mutable {
            ret.setSuccess(true);
        },
        TDApi::array<TDApi::int32>{}, name.toStdString()
    );

    return ret;
}
