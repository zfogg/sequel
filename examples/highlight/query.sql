-- A report with quoted identifiers, parameters, literals and a CTE.
with totals as (select account_id, sum(amount::decimal) as total from payments where created_at >= :since and status = 'paid & settled' group by account_id) select "account_id", total, true as active, null as note from totals where total > 123.45 order by total desc limit 20;
