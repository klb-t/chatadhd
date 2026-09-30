# Configured frontier replay: fix retest 05

Re-run exactly the two already declared scripted cases from protocol 04 against
the producer's new code. Immutable source copies, first responses, ledgers and
results are new files; preserve original exceptions. Provider/model, fake output,
fake cost, configured response cap, budget and reservations are unchanged.
Code/dependency identity is the only changed experimental variable. Expected:
replay now honors both configured domains and retains all fake reported costs
even when the padded semantic output is rejected. This is offline mechanism
verification with actual paid requests/cost zero, not frontier quality/billing.
