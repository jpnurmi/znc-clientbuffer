/*
 * Copyright (C) 2004-2014  See the AUTHORS file for details.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <znc/Modules.h>
#include <znc/IRCNetwork.h>
#include <znc/Client.h>
#include <znc/Utils.h>
#include <znc/User.h>
#include <znc/Chan.h>
#include <znc/znc.h>
#include <sys/time.h>

static const char* ServerTimeFormat = "%Y-%m-%dT%H:%M:%S";

class CClientBufferMod : public CModule
{
public:
    MODCONSTRUCTOR(CClientBufferMod)
    {
        AddHelpCommand();
        AddCommand("AddClient", static_cast<CModCommand::ModCmdFunc>(&CClientBufferMod::OnAddClientCommand), "<identifier>", "Add a client.");
        AddCommand("DelClient", static_cast<CModCommand::ModCmdFunc>(&CClientBufferMod::OnDelClientCommand), "<identifier>", "Delete a client.");
        AddCommand("ListClients", static_cast<CModCommand::ModCmdFunc>(&CClientBufferMod::OnListClientsCommand), "", "List known clients.");
    }

    void OnAddClientCommand(const CString& line);
    void OnDelClientCommand(const CString& line);
    void OnListClientsCommand(const CString& = "");

    virtual EModRet OnSendToClient(CString& line, CClient& client) override;

    virtual EModRet OnChanBufferStarting(CChan& chan, CClient& client) override;
    virtual EModRet OnChanBufferEnding(CChan& chan, CClient& client) override;
    virtual EModRet OnChanBufferPlayLine(CChan& chan, CClient& client, CString& line, const timeval& tv) override;
    virtual EModRet OnPrivBufferPlayLine(CClient& client, CString& line, const timeval& tv) override;

private:
    bool AddClient(const CString& identifier);
    bool DelClient(const CString& identifier);
    bool HasClient(const CString& identifier);

    timeval GetTimestamp(const CString& identifier);
    timeval GetTimestamp(const CBuffer& buffer) const;
    bool HasSeenTimestamp(const CString& identifier, const timeval& tv);
    bool UpdateTimestamp(const CString& identifier, const timeval& tv);
};

void CClientBufferMod::OnAddClientCommand(const CString& line)
{
    const CString identifier = line.Token(1);
    if (identifier.empty()) {
        PutModule("Usage: AddClient <identifier>");
        return;
    }
    if (HasClient(identifier)) {
        PutModule("Client already exists: " + identifier);
        return;
    }
    AddClient(identifier);
    PutModule("Client added: " + identifier);
}

void CClientBufferMod::OnDelClientCommand(const CString& line)
{
    const CString identifier = line.Token(1);
    if (identifier.empty()) {
        PutModule("Usage: DelClient <identifier>");
        return;
    }
    if (!HasClient(identifier)) {
        PutModule("Unknown client: " + identifier);
        return;
    }
    DelClient(identifier);
    PutModule("Client removed: " + identifier);
}

void CClientBufferMod::OnListClientsCommand(const CString&)
{
    const CString& current = GetClient()->GetIdentifier();

    CTable table;
    table.AddColumn("Client");
    table.AddColumn("Connected");
    table.AddColumn("Last seen message");
    for (MCString::iterator it = BeginNV(); it != EndNV(); ++it) {
        table.AddRow();
        if (it->first == current)
            table.SetCell("Client",  "*" + it->first);
        else
            table.SetCell("Client",  it->first);
        table.SetCell("Connected", CString(!GetNetwork()->FindClients(it->first).empty()));
        table.SetCell("Last seen message", CUtils::FormatTime(GetTimestamp(it->first).tv_sec, ServerTimeFormat, ""));
    }

    if (table.empty())
        PutModule("No identified clients");
    else
        PutModule(table);
}

CModule::EModRet CClientBufferMod::OnSendToClient(CString& line, CClient& client)
{
    const CString& identifier = client.GetIdentifier();
    if (client.IsReady() && HasClient(identifier)) {
        timeval tv;
        gettimeofday(&tv, NULL);
        UpdateTimestamp(identifier, tv);
    }
    return CONTINUE;
}

CModule::EModRet CClientBufferMod::OnChanBufferStarting(CChan& chan, CClient& client)
{
    if (client.HasServerTime())
        return HALTCORE;

    const CString& identifier = client.GetIdentifier();
    if (!client.IsReady() && HasClient(identifier)) {
        // let "Buffer Playback..." message through?
        const CBuffer& buffer = chan.GetBuffer();
        if (!buffer.IsEmpty() && HasSeenTimestamp(identifier, GetTimestamp(buffer)))
            return HALTCORE;
    }
    return CONTINUE;
}

CModule::EModRet CClientBufferMod::OnChanBufferEnding(CChan& chan, CClient& client)
{
    if (client.HasServerTime())
        return HALTCORE;

    const CString& identifier = client.GetIdentifier();
    if (!client.IsReady() && HasClient(identifier)) {
        // let "Buffer Complete" message through?
        const CBuffer& buffer = chan.GetBuffer();
        if (!buffer.IsEmpty() && !UpdateTimestamp(identifier, GetTimestamp(buffer)))
            return HALTCORE;
    }
    return CONTINUE;
}

CModule::EModRet CClientBufferMod::OnChanBufferPlayLine(CChan& chan, CClient& client, CString& line, const timeval& tv)
{
    const CString& identifier = client.GetIdentifier();
    if (!client.IsReady() && HasClient(identifier) && HasSeenTimestamp(identifier, tv))
        return HALTCORE;
    return CONTINUE;
}

CModule::EModRet CClientBufferMod::OnPrivBufferPlayLine(CClient& client, CString& line, const timeval& tv)
{
    const CString& identifier = client.GetIdentifier();
    if (!client.IsReady() && HasClient(identifier) && !UpdateTimestamp(identifier, tv))
        return HALTCORE;
    return CONTINUE;
}

bool CClientBufferMod::AddClient(const CString& identifier)
{
    return SetNV(identifier, GetNV(identifier));
}

bool CClientBufferMod::DelClient(const CString& identifier)
{
    return DelNV(identifier);
}

bool CClientBufferMod::HasClient(const CString& identifier)
{
    return !identifier.empty() && FindNV(identifier) != EndNV();
}

timeval CClientBufferMod::GetTimestamp(const CString& identifier)
{
    timeval tv;
    double timestamp = GetNV(identifier).ToDouble();
    tv.tv_sec = timestamp;
    tv.tv_usec = (timestamp - tv.tv_sec) * 1000000;
    return tv;
}

timeval CClientBufferMod::GetTimestamp(const CBuffer& buffer) const
{
    return buffer.GetBufLine(buffer.Size() - 1).GetTime();
}

bool CClientBufferMod::HasSeenTimestamp(const CString& identifier, const timeval& tv)
{
    const timeval seen = GetTimestamp(identifier);
    return timercmp(&seen, &tv, >);
}

bool CClientBufferMod::UpdateTimestamp(const CString& identifier, const timeval& tv)
{
    if (!HasSeenTimestamp(identifier, tv)) {
        double timestamp = tv.tv_sec + tv.tv_usec / 1000000.0;
        return SetNV(identifier, CString(timestamp));
    }
    return false;
}

NETWORKMODULEDEFS(CClientBufferMod, "Client specific buffer playback")
