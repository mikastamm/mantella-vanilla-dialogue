Makes Mantella conversations aware of things said in the vanilla dialogue system through the Event system.

Excerpt from ysolda.json to how vanilla dialogue is added. 
```
        {
            "role": "user",
            "content": "(Prisoner: What do you know of the Khajiit?; Ysolda: About the same as everyone else. They're the cat-folk of Elsweyr. Great warriors, good traders. The way I hear it, Elsweyr ain't nothing like Skyrim. It's got tropical forests and dusty badlands. It sounds awful!) I'm sorry, I was just debugging, don't take it personally."
        },
```

## How it works
Situation: _Player selects a line to say in the dialogue menu_. Depending on the state of mantella, there are 3 different things that can happen:

**Player is in a mantella conversation with that npc** -> AddEvent is called

**Player is not in a conversation** -> Dialogue Exchange gets stored to SKSE save file and sent the next time a Mantella conversation with that NPC starts
    - Theres a limit in place of ~5MB of stored dialogue line, exceeding that will wipe all stored vanilla dialogue
    - We potentially store all spoken vanilla dialogue for eternity, if no mantella conversation is ever started with an NPC the user previously had dialogue with

**Player is in a conversation, but the NPC they are in dialogue with is not part of it** -> AddEvent is called & the voiceline is stored and resent when that NPC enters the conversation or the next time a conversation with them is started

---

Note: As soon as a dialogue item is selected by the player, all the sentences get sent to Mantella that the NPC will say in response. even if the player exits the dialogue early, Mantella will still be aware of all the lines the NPC would have said.

## SKSE Plugin

All the logic for this is in an SKSE plug-in, The code for which can be found [here](https://github.com/mikastamm/mantella-vanilla-dialogue).

I have a prebuilt MantellaDialogue.dll there, but you are of course free to build it from source yourself, before including it in the main repo. 

## Configuration

There are some extra configuration options possible through the `SKSE/Plugins/MantellaDialogue.ini` file.
Of interest are mostly the blacklist items, as some dialogue (eg. Configuration stuff from mods) would just confuse the ai, so we can blacklist it here.
```ini
; Toggles Dialogue Tracking (Also available in MCM)
EnableVanillaDialogueTracking=true

; If the NPC's reply is less than FilterShortRepliesMinWordCount, Do not send it to Mantella.
; can help filter out some menus and mod related dialogue options.
FilterShortReplies=false
FilterShortRepliesMinWordCount=4
; filters all player lines and npc responses for greetings that are non unique (ie. can be triggered each time the player starts a conversation with that NPC.)
FilterNonUniqueGreetings=true
; If the NPCs response or line is included in one of these, discard both the player's line and the NPCs line.
NPCLineBlacklist=Can I help you?, Farewell, See you later
; If the players line is included in these, discard both the players and the NPCs line.
PlayerLineBlacklist=Stage1Hello, I want you to.., Goodbye. (Remove from Mantella conversation), DialogueGenericHello

; Add the names of the NPCs here for which you do not want to track dialogue (comma seperated)
; The Names must be the same as they appear in the game.
NPCNamesToIgnore=
```
## Known Issues
- When you start the mantella conversation and have previously saved vanilla dialogue for that character, it is sent to mantella and removed from the storage, so it wont get sent a second time. If you then end the conversation without saying anything, or it is too short for summarization, those dialogue lines will be lost.
    - This also happens if the conversation ends due to an error & is too short to summarize or does not get summarized due to the error
- When too many events are in the queue, vanilla dialogue lines might be culled and thus never sent to mantella
- If an error occurs while generating the response, and it has vanilla dialogue events attached, that dialogue will never get sent to mantella

### Testing
So far I tested it on VR and AE using Windows. Hopefully it will work on linux as well. 
