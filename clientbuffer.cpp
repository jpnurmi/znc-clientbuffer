/*
 * Copyright (C) 2015 J-P Nurmi
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <znc/Modules.h>
#include <znc/IRCNetwork.h>
#include <znc/Client.h>
#include <znc/Utils.h>
#include <znc/User.h>
#include <znc/Chan.h>
#include <znc/znc.h>
#include <znc/version.h>
#include <sys/time.h>

#if (VERSION_MAJOR < 1) || (VERSION_MAJOR == 1 && VERSION_MINOR < 6)
#error The clientbuffer module requires ZNC version 1.6.0 or later.
#endif

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
    void OnListClientsCommand(const CString& line);

    virtual void OnClientLogin();

    virtual EModRet OnUserRaw(CString& line) override;
    virtual EModRet OnSendToClient(CString& line, CClient& client) override;

    virtual EModRet OnChanBufferStarting(CChan& chan, CClient& client) override;
    virtual EModRet OnChanBufferEnding(CChan& chan, CClient& client) override;
    virtual EModRet OnChanBufferPlayLine2(CChan& chan, CClient& client, CString& line, const timeval& tv) override;
    virtual EModRet OnPrivBufferPlayLine2(CClient& client, CString& line, const timeval& tv) override;

private:
    bool AddClient(const CString& identifier);
    bool DelClient(const CString& identifier);
    bool HasClient(const CString& identifier);

    bool ParseMessage(const CString& line, CNick& nick, CString& cmd, CString& target) const;
    timeval GetTimestamp(const CString& identifier, const CString& target);
    timeval GetTimestamp(const CBuffer& buffer) const;
    bool HasSeenTimestamp(const CString& identifier, const CString& target, const timeval& tv);
    bool UpdateTimestamp(const CString& identifier, const CString& target, const timeval& tv);
    void UpdateTimestamp(const CClient* client, const CString& target);
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

void CClientBufferMod::OnListClientsCommand(const CString& line)
{
    const CString& current = GetClient()->GetIdentifier();

    CTable table;
    table.AddColumn("Client");
    table.AddColumn("Connected");

    for (MCString::iterator it = BeginNV(); it != EndNV(); ++it) {
        if (it->first.Find("/") == CString::npos) {
            table.AddRow();
            if (it->first == current)
                table.SetCell("Client",  "*" + it->first);
            else
                table.SetCell("Client",  it->first);
            table.SetCell("Connected", CString(!GetNetwork()->FindClients(it->first).empty()));
        }
    }

    if (table.empty())
        PutModule("No identified clients");
    else
        PutModule(table);
}

void CClientBufferMod::OnClientLogin()
{
    const CString& current = GetClient()->GetIdentifier();

    if (!HasClient(current) && GetArgs().Token(0).Equals("autoadd", CString::CaseInsensitive)) {
        AddClient(current);
    }
}

CModule::EModRet CClientBufferMod::OnUserRaw(CString& line)
{
    CClient* client = GetClient();
    if (client) {
        CNick nick; CString cmd, target;
        // make sure not to update the timestamp for a channel when joining it
        if (ParseMessage(line, nick, cmd, target) && !cmd.Equals("JOIN"))
            UpdateTimestamp(client, target);
    }
    return CONTINUE;
}

CModule::EModRet CClientBufferMod::OnSendToClient(CString& line, CClient& client)
{
    CIRCNetwork* network = GetNetwork();
    if (network) {
        CNick nick; CString cmd, target;
        // make sure not to update the timestamp for a channel when attaching it
        if (ParseMessage(line, nick, cmd, target)) {
            CChan* chan = network->FindChan(target);
            if (!chan || !chan->IsDetached())
                UpdateTimestamp(&client, target);
        }
    }
    return CONTINUE;
}

CModule::EModRet CClientBufferMod::OnChanBufferStarting(CChan& chan, CClient& client)
{
    if (client.HasServerTime())
        return HALTCORE;

    const CString& identifier = client.GetIdentifier();
    if (HasClient(identifier)) {
        // let "Buffer Playback..." message through?
        const CBuffer& buffer = chan.GetBuffer();
        if (!buffer.IsEmpty() && HasSeenTimestamp(identifier, chan.GetName(), GetTimestamp(buffer)))
            return HALTCORE;
    }
    return CONTINUE;
}

CModule::EModRet CClientBufferMod::OnChanBufferEnding(CChan& chan, CClient& client)
{
    if (client.HasServerTime())
        return HALTCORE;

    const CString& identifier = client.GetIdentifier();
    if (HasClient(identifier)) {
        // let "Buffer Complete" message through?
        const CBuffer& buffer = chan.GetBuffer();
        if (!buffer.IsEmpty() && !UpdateTimestamp(identifier, chan.GetName(), GetTimestamp(buffer)))
            return HALTCORE;
    }
    return CONTINUE;
}

CModule::EModRet CClientBufferMod::OnChanBufferPlayLine2(CChan& chan, CClient& client, CString& line, const timeval& tv)
{
    const CString& identifier = client.GetIdentifier();
    if (HasClient(identifier) && HasSeenTimestamp(identifier, chan.GetName(), tv))
        return HALTCORE;
    return CONTINUE;
}

CModule::EModRet CClientBufferMod::OnPrivBufferPlayLine2(CClient& client, CString& line, const timeval& tv)
{
    const CString& identifier = client.GetIdentifier();
    if (HasClient(identifier)) {
        CNick nick; CString cmd, target;
        if (ParseMessage(line, nick, cmd, target) && !UpdateTimestamp(identifier, target, tv))
            return HALTCORE;
    }
    return CONTINUE;
}

bool CClientBufferMod::AddClient(const CString& identifier)
{
    return SetNV(identifier, "");
}

bool CClientBufferMod::DelClient(const CString& identifier)
{
    SCString keys;
    for (MCString::iterator it = BeginNV(); it != EndNV(); ++it) {
        const CString client = it->first.Token(0, false, "/");
        if (client.Equals(identifier))
            keys.insert(it->first);
    }
    bool success = true;
    for (const CString& key : keys)
        success &= DelNV(key);
    return success;
}

bool CClientBufferMod::HasClient(const CString& identifier)
{
    return !identifier.empty() && FindNV(identifier) != EndNV();
}

bool CClientBufferMod::ParseMessage(const CString& line, CNick& nick, CString& cmd, CString& target) const
{
    // discard message tags
    CString msg = line;
    if (msg.StartsWith("@"))
        msg = msg.Token(1, true);

    CString rest;
    if (msg.StartsWith(":")) {
        nick = CNick(msg.Token(0).TrimPrefix_n());
        cmd = msg.Token(1);
        rest = msg.Token(2, true);
    } else {
        cmd = msg.Token(0);
        rest = msg.Token(1, true);
    }

    if (cmd.length() == 3 && isdigit(cmd[0]) && isdigit(cmd[1]) && isdigit(cmd[2])) {
        // must block the following numeric replies that are automatically sent on attach:
        // RPL_NAMREPLY, RPL_ENDOFNAMES, RPL_TOPIC, RPL_TOPICWHOTIME...
        unsigned int num = cmd.ToUInt();
        if (num == 353) // RPL_NAMREPLY
            target = rest.Token(2);
        else
            target = rest.Token(1);
    } else if (cmd.Equals("PRIVMSG") || cmd.Equals("NOTICE") || cmd.Equals("JOIN") || cmd.Equals("PART") || cmd.Equals("MODE") || cmd.Equals("KICK") || cmd.Equals("TOPIC")) {
        target = rest.Token(0).TrimPrefix_n(":");
    }

    return !target.empty() && !cmd.empty();
}

timeval CClientBufferMod::GetTimestamp(const CString& identifier, const CString& target)
{
    timeval tv;
    double timestamp = GetNV(identifier + "/" + target).ToDouble();
    tv.tv_sec = timestamp;
    tv.tv_usec = (timestamp - tv.tv_sec) * 1000000;
    return tv;
}

timeval CClientBufferMod::GetTimestamp(const CBuffer& buffer) const
{
    return buffer.GetBufLine(buffer.Size() - 1).GetTime();
}

bool CClientBufferMod::HasSeenTimestamp(const CString& identifier, const CString& target, const timeval& tv)
{
    const timeval seen = GetTimestamp(identifier, target);
    return timercmp(&seen, &tv, >);
}

bool CClientBufferMod::UpdateTimestamp(const CString& identifier, const CString& target, const timeval& tv)
{
    if (!HasSeenTimestamp(identifier, target, tv)) {
        double timestamp = tv.tv_sec + tv.tv_usec / 1000000.0;
        return SetNV(identifier + "/" + target, CString(timestamp));
    }
    return false;
}

void CClientBufferMod::UpdateTimestamp(const CClient* client, const CString& target)
{
    if (client && !client->IsPlaybackActive()) {
        const CString& identifier = client->GetIdentifier();
        if (HasClient(identifier)) {
            timeval tv;
            gettimeofday(&tv, NULL);
            UpdateTimestamp(identifier, target, tv);
        }
    }
}

template<> void TModInfo<CClientBufferMod>(CModInfo& info) {
	info.SetWikiPage("Clientbuffer");
	info.SetHasArgs(true);
}

NETWORKMODULEDEFS(CClientBufferMod, "Client specific buffer playback")
