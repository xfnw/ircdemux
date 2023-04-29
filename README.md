# ircdemux

ircdemux is a fast irc client demultiplexer in c using
epoll. it manages multiple irc connections and distributes
lines to them.

# usage

ircdemux takes input from stdin. a fifo 'named pipe' can be
used for long-running instances. `exec 3>fifo` or simply
running `cat >fifo` in another terminal can be used to keep
it open over multiple accesses. though this is not strictly
necessary, it may do weird things if the fifo gets 'closed'.

```sh
mkfifo irc
<irc ./ircdemux &
exec 3>irc
echo '/j##test' >irc
echo '/circ.libera.chat 6667 hello-ircdemux' >irc
```

lines that are not commands will be sent to the first
available connection.

## commands

lines beginning with `/` are considered control commands.
note that each command name is one byte and *does not* need
a space after it.

`<this>` means a required parameter, while `[this]` is optional.

### a
```
/a<line>
```

where `<line>` is a raw IRC line to send to all writable connections

### b
```
/b<number>
```

where `<number>` is a positive integer greater than zero for
the number of lines to send at once to a single client. if
your message order is getting mixed up, this option can
usually help.

### c
```
/c<host> <port> <nick> [username] [realname]
```

where `<host>` is the hostname of the irc server you want to
connect to, `<port>` is the port number, `<nick>` is the
nickname to use, `[username]` is the username/ident to use,
and `[realname]` is the realname/gecos to use.

if left unspecified, `[username]` will default to the
nickname, and `[realname]` will default to the username.

### j
```
/j[channel]
```

where `[channel]` is the channel to JOIN after receiving
RPL_WELCOME (001). this will not effect already-registered
connections.

if left empty, autojoining a channel on RPL_WELCOME will be
disabled.

### s
```
/s
```

re-seed the questionable pseudorandom number generator (for
changing duplcate nicknames) with the time, as its period is
a bit small, especially with short nicknames

### t
```
/t[line]
```

where `[line]` is the template to prefix all non-command
lines with.

if left empty, non-command lines will be sent, unaltered, to
the irc server.

for example
```
/tPRIVMSG ##test :
hello!
there!
```
will send "hello!" and "there!" messages to `##test`

# bugs

- if the input gets 'closed', ircdemux can get stuck
  repeatedly re-reading the same input.
  
- while ircdemux itself will never read a line longer than
  512 bytes, it is possible to make it send a line longer
  than that by using a template.

- ircdemux is bad at detecting connection loss, since it
  never sends lines to connections that are not advertised
  as writable.

- ircdemux does not currently support tls encryption.

