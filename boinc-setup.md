## 1. Install BOINC

Install the BOINC client:

```bash
sudo apt install -y boinc-client
```

---

## 2. Allow controlling boinccmd without sudo

Add your user to the `boinc` group:

```bash
sudo usermod -aG boinc "$USER"
```

## 3. Find the BOINC Working Directory

```bash
BOINC_WORK_DIR="$(systemctl show -p WorkingDirectory --value boinc-client)"
echo "$BOINC_WORK_DIR"
```

The PrimeGrid project directory will eventually be:

```bash
$BOINC_WORK_DIR/projects/www.primegrid.com
```

---

## 4. Create a PrimeGrid Account

Create primegrid account using the PrimeGrid website.

After creating the account, log in and open account page.

### Use the weak account key

From PrimeGrid account page, locate:

```text
Account keys
```

and copy the **weak account key**.

---

## 5. Attach BOINC to PrimeGrid

Attach the local BOINC client to PrimeGrid with:

```bash
boinccmd --project_attach http://www.primegrid.com/ WEAK_ACCOUNT_KEY
```

BOINC should contact PrimeGrid and create the project directory automatically.

---

## 6. Verify the PrimeGrid Attachment

Run:

```bash
boinccmd --get_project_status
```

You should see a section similar to:

```text
======== Projects ========
1) -----------
   name: PrimeGrid
   master URL: http://www.primegrid.com/
   user_name: ...
   resource share: ...
```

Also check that the project directory exists:

```bash
BOINC_WORK_DIR="$(systemctl show -p WorkingDirectory --value boinc-client)"
ls "$BOINC_WORK_DIR/projects/www.primegrid.com"
```

At this point, the BOINC client is connected to PrimeGrid account.

---

## 7. Configure PrimeGrid for AP27

Log in to the PrimeGrid website and open your **PrimeGrid preferences**.

Under the application selection section:

1. Enable **AP27**
2. Disable other PrimeGrid applications
3. Disable:

```text
If no work for selected applications is available,
accept work from other applications?
```

Save the preferences.

Then update the local client to contact PrimeGrid:

```bash
boinccmd --project http://www.primegrid.com/ update
```

---

## 8. Make Sure Work Fetch Is Enabled

Check the PrimeGrid project state:

```bash
boinccmd --get_project_status
```

Look for:

```text
don't request more work: no
```

If it says:

```text
don't request more work: yes
```

enable work fetching again:

```bash
boinccmd --project http://www.primegrid.com/ allowmorework
boinccmd --project http://www.primegrid.com/ update
```

---

## 9. Install `ap27_v3d`

Follow instructions in [README.md](README.md) to build from source

Or download executable and dependencies in **Release** and copy all the files to `$BOINC_WORK_DIR/projects/www.primegrid.com`

---

## 10. Request AP27 Work

After installing `ap27_v3d`, make sure work fetch is enabled and contact PrimeGrid:

```bash
boinccmd --project http://www.primegrid.com/ allowmorework
boinccmd --project http://www.primegrid.com/ update
```

Wait a few seconds and check:

```bash
boinccmd --get_tasks
```

An AP27 task should eventually appear similar to:

```text
name: ap27_XXXXXXXX_X
WU name: ap27_XXXXXXXX
project URL: http://www.primegrid.com/
state: downloaded
scheduler state: scheduled
active_task_state: EXECUTING
```

---

## 11. Check BOINC Logs

If no tasks arrive, inspect the BOINC service log:

```bash
journalctl -u boinc-client -n 100 --no-pager
```

To watch the log live:

```bash
journalctl -u boinc-client -f
```
