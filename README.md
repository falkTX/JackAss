JackAss
=======

<p>
    JackAss is a VST plugin that provides JACK-MIDI support for VST hosts.<br/>
    Simply load the plugin in your favourite host to get a JACK-MIDI port.<br/>
    Each new plugin instance creates a new MIDI port.<br/>
</p>
<p>
    <b>Here's JackAss loaded in FL Studio:</b><br/>
    <a href="https://kx.studio/screenshots/news/jackass_flstudio.png"><img src="http://kxstudio.sourceforge.net/screenshots/news/jackass_flstudio_crop.png" alt=""/></a><br/>
    <br/>
    <b>And an example setup in Carla for it:</b><br/>
    <a href="https://kx.studio/screenshots/news/jackass_carla.png"><img src="http://kxstudio.sourceforge.net/screenshots/news/jackass_carla_crop.png" alt=""/></a><br/>
</p>
<p>
    JackAss sends the notes from the host to its JACK-MIDI port.<br/>
    It also exposes 50 parameters, which send a MIDI CC message when changed.<br/>
    You can use this to easily control external applications that accept JACK-MIDI input and possibly CC for automation (like Carla).<br/>
</p>
<p>
    Additionally there's a JackAssFX plugin, which only exposes parameters to send as MIDI CC, in case you don't need MIDI/notes.<br/>
</p>
<p>
    JackAss currently has builds for Linux, MacOS and Windows, all 32bit and 64bit. Just follow
        <a href="https://github.com/falkTX/JackAss/releases" class="external free" rel="nofollow" target="_blank">this link</a>.<br/>
    As a bonus, you also get special Wine builds - load it in a Windows application running in Linux via Wine and you get a real, native JACK-MIDI port from it!<br/>
</p>
<p>
    PS: Why JackAss? Because it outputs to JACK. ;)
</p>
